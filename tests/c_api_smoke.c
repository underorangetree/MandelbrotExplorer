#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mandelbrot_explorer/mandelbrot_explorer.h"

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__);  \
            return 1;                                                                       \
        }                                                                                   \
    } while (0)

static unsigned long read_u16(const unsigned char* data) {
    return (unsigned long)data[0] | ((unsigned long)data[1] << 8);
}

static unsigned long read_u32(const unsigned char* data) {
    return read_u16(data) | (read_u16(data + 2) << 16);
}

static size_t read_file(const char* path, unsigned char* buffer, size_t capacity) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    const size_t size = fread(buffer, 1, capacity, file);
    fclose(file);
    return size;
}

int main(void) {
    CHECK(mb_version() != NULL);
    CHECK(mb_last_error() != NULL);

    /* Invalid arguments are reported through the status/error API. */
    CHECK(mb_create(0, 0, 0, 0) == NULL);
    CHECK(mb_last_error()[0] != '\0');

    /* NULL contexts and unknown enum values are reported, never dereferenced. */
    CHECK(mb_width(NULL) == 0 && mb_height(NULL) == 0);
    CHECK(mb_storage_width(NULL) == 0 && mb_thread_count(NULL) == 0);
    CHECK(mb_kernel_name(NULL) == NULL && mb_frame_data(NULL) == NULL);
    CHECK(mb_render(NULL) == MB_INVALID_ARGUMENT);
    CHECK(mb_render_into(NULL, NULL, 0) == MB_INVALID_ARGUMENT);
    CHECK(mb_render_outputs(NULL, NULL, 0, NULL, 0) == MB_INVALID_ARGUMENT);
    CHECK(mb_set_view(NULL, 1.0, 0.0, 0.0) == MB_INVALID_ARGUMENT);
    CHECK(mb_set_kernel(NULL, "scalar") == MB_INVALID_ARGUMENT);
    CHECK(mb_set_pixel_order(NULL, MB_BGR) == MB_INVALID_ARGUMENT);
    CHECK(mb_write_image(NULL, "never.bmp", MB_IMAGE_BMP) == MB_INVALID_ARGUMENT);
    CHECK(mb_render_frame(NULL, NULL, 0, NULL, 0) == MB_INVALID_ARGUMENT);
    CHECK(mb_render_image(NULL, "never.bmp", MB_IMAGE_BMP) == MB_INVALID_ARGUMENT);
    mb_cancel(NULL); /* safe */

    mb_context* ctx = mb_create(64, 32, 64, 2);
    CHECK(ctx != NULL);
    CHECK(mb_width(ctx) == 64);
    CHECK(mb_height(ctx) == 32);
    CHECK(mb_storage_width(ctx) >= 64);
    CHECK(mb_thread_count(ctx) == 2);
    CHECK(mb_kernel_name(ctx) != NULL);

    CHECK(mb_set_view(ctx, 1.0, -0.5, 0.0) == MB_OK);
    CHECK(mb_set_view(ctx, 0.0, 0.0, 0.0) == MB_INVALID_ARGUMENT);
    CHECK(mb_set_view(ctx, 1.0, NAN, 0.0) == MB_INVALID_ARGUMENT);
    CHECK(mb_set_kernel(ctx, "scalar") == MB_OK);
    CHECK(strcmp(mb_kernel_name(ctx), "scalar") == 0);
    CHECK(mb_set_kernel(ctx, "bogus") == MB_INVALID_ARGUMENT);
    CHECK(mb_set_kernel(ctx, NULL) == MB_INVALID_ARGUMENT);
    CHECK(mb_set_pixel_order(ctx, (mb_pixel_order)9) == MB_INVALID_ARGUMENT);

    /* threads = 0 selects the hardware concurrency. */
    mb_context* automatic = mb_create(8, 8, 8, 0);
    CHECK(automatic != NULL);
    CHECK(mb_thread_count(automatic) >= 1);
    mb_destroy(automatic);

    /* Cancellation is sticky; the next render clears it first. */
    mb_cancel(ctx);
    CHECK(mb_render(ctx) == MB_OK);
    const unsigned char* frame = mb_frame_data(ctx);
    CHECK(frame != NULL);

    const size_t stride = (size_t)mb_storage_width(ctx) * 3;
    unsigned char* buffer = (unsigned char*)malloc((size_t)mb_height(ctx) * stride);
    CHECK(buffer != NULL);
    CHECK(mb_render_into(ctx, buffer, stride) == MB_OK);
    CHECK(memcmp(frame, buffer, (size_t)mb_height(ctx) * stride) == 0);
    free(buffer);

    /* A padded stride is allowed and row 0 must match. */
    unsigned char* padded = (unsigned char*)malloc((size_t)mb_height(ctx) * (stride + 30));
    CHECK(padded != NULL);
    CHECK(mb_render_into(ctx, padded, stride + 30) == MB_OK);
    CHECK(memcmp(frame, padded, stride) == 0);
    free(padded);

    CHECK(mb_render_into(ctx, NULL, stride) == MB_INVALID_ARGUMENT);

    /* Iteration counts: the view center is (0,0), which is inside the set. */
    const size_t iteration_stride = (size_t)mb_width(ctx) * sizeof(int32_t);
    int32_t* iterations = (int32_t*)malloc((size_t)mb_height(ctx) * iteration_stride);
    CHECK(iterations != NULL);
    CHECK(mb_render_outputs(ctx, NULL, 0, iterations, iteration_stride) == MB_OK);
    CHECK(iterations[(mb_height(ctx) / 2) * mb_width(ctx) + mb_width(ctx) / 2] == 64);

    /* Colors and iterations together match the single-output renders. */
    unsigned char* both_colors = (unsigned char*)malloc((size_t)mb_height(ctx) * stride);
    int32_t* both_iterations = (int32_t*)malloc((size_t)mb_height(ctx) * iteration_stride);
    CHECK(both_colors != NULL && both_iterations != NULL);
    CHECK(mb_render_outputs(ctx, both_colors, stride, both_iterations, iteration_stride) == MB_OK);
    CHECK(memcmp(frame, both_colors, (size_t)mb_height(ctx) * stride) == 0);
    CHECK(memcmp(iterations, both_iterations, (size_t)mb_height(ctx) * iteration_stride) == 0);
    CHECK(mb_render_outputs(ctx, NULL, 0, NULL, 0) == MB_INVALID_ARGUMENT);
    free(both_iterations);
    free(both_colors);

    /* RGB output swaps the red and blue channels of the BGR render. */
    CHECK(mb_set_pixel_order(ctx, MB_RGB) == MB_OK);
    unsigned char* rgb = (unsigned char*)malloc((size_t)mb_height(ctx) * stride);
    CHECK(rgb != NULL);
    CHECK(mb_render_into(ctx, rgb, stride) == MB_OK);
    for (size_t i = 0; i + 2 < (size_t)mb_height(ctx) * stride; i += 3) {
        CHECK(rgb[i] == frame[i + 2] && rgb[i + 1] == frame[i + 1] && rgb[i + 2] == frame[i]);
    }
    CHECK(mb_set_pixel_order(ctx, MB_BGR) == MB_OK);
    free(rgb);

    /* Image files (dependency-free BMP / PNM writers): validate the header and
     * every pixel against the rendered BGR frame. */
    CHECK(mb_write_image(ctx, "c_api_smoke.bmp", MB_IMAGE_BMP) == MB_OK);
    CHECK(mb_write_image(ctx, "c_api_smoke.ppm", MB_IMAGE_PNM) == MB_OK);
    {
        const int width = mb_width(ctx);
        const int height = mb_height(ctx);
        const size_t row_size = (size_t)((width * 3 + 3) / 4) * 4; /* BMP rows pad to 4 bytes */
        const size_t bmp_size = 54 + row_size * (size_t)height;
        unsigned char* bmp = (unsigned char*)malloc(bmp_size + 16);
        unsigned char* pnm = (unsigned char*)malloc((size_t)width * (size_t)height * 3 + 64);
        CHECK(bmp != NULL && pnm != NULL);

        CHECK(read_file("c_api_smoke.bmp", bmp, bmp_size + 16) == bmp_size);
        CHECK(bmp[0] == 'B' && bmp[1] == 'M');
        CHECK(read_u32(bmp + 2) == (unsigned long)bmp_size); /* file size */
        CHECK(read_u32(bmp + 10) == 54);                     /* pixel data offset */
        CHECK(read_u32(bmp + 14) == 40);                     /* DIB header size */
        CHECK(read_u32(bmp + 18) == (unsigned long)width && read_u32(bmp + 22) == (unsigned long)height);
        CHECK(read_u16(bmp + 28) == 24);                     /* bits per pixel */
        for (int y = 0; y < height; ++y) {
            /* BMP stores rows bottom-up, in BGR order. */
            const unsigned char* file_row = bmp + 54 + row_size * (size_t)(height - 1 - y);
            const unsigned char* frame_row = frame + (size_t)y * stride;
            for (int x = 0; x < width; ++x) {
                for (int channel = 0; channel < 3; ++channel) {
                    CHECK(file_row[x * 3 + channel] == frame_row[x * 3 + channel]);
                }
            }
            for (size_t padding = (size_t)width * 3; padding < row_size; ++padding) {
                CHECK(file_row[padding] == 0);
            }
        }

        char pnm_header[64];
        const int pnm_header_length = snprintf(pnm_header, sizeof(pnm_header), "P6\n%d %d\n255\n", width, height);
        CHECK(pnm_header_length > 0);
        CHECK(read_file("c_api_smoke.ppm", pnm, (size_t)width * (size_t)height * 3 + 64) ==
              (size_t)pnm_header_length + (size_t)width * (size_t)height * 3);
        CHECK(memcmp(pnm, pnm_header, (size_t)pnm_header_length) == 0);
        for (int y = 0; y < height; ++y) {
            /* PNM stores rows top-down, in RGB order. */
            const unsigned char* file_row = pnm + pnm_header_length + (size_t)y * (size_t)width * 3;
            const unsigned char* frame_row = frame + (size_t)y * stride;
            for (int x = 0; x < width; ++x) {
                CHECK(file_row[x * 3 + 0] == frame_row[x * 3 + 2]);
                CHECK(file_row[x * 3 + 1] == frame_row[x * 3 + 1]);
                CHECK(file_row[x * 3 + 2] == frame_row[x * 3 + 0]);
            }
        }
        free(pnm);
        free(bmp);
        remove("c_api_smoke.bmp");
        remove("c_api_smoke.ppm");
    }

    /* Unwritable paths and unknown formats fail without crashing. */
    CHECK(mb_write_image(ctx, NULL, MB_IMAGE_BMP) == MB_INVALID_ARGUMENT);
    CHECK(mb_write_image(ctx, "c_api_smoke.bmp", (mb_image_format)9) == MB_INVALID_ARGUMENT);
    CHECK(mb_write_image(ctx, "no_such_directory/out.bmp", MB_IMAGE_BMP) == MB_INTERNAL_ERROR);
    CHECK(mb_write_image(ctx, "no_such_directory/out.ppm", MB_IMAGE_PNM) == MB_INTERNAL_ERROR);
    CHECK(mb_last_error()[0] != '\0');

    /* One-shot parameter API matches the context-based render. */
    {
        mb_render_params params;
        memset(&params, 0, sizeof(params));
        params.struct_size = sizeof(params);
        params.width = 64;
        params.height = 32;
        params.max_iterations = 64;
        params.threads = 2;
        params.zoom = 1.0;
        params.center_x = -0.5;
        params.center_y = 0.0;
        params.kernel = "scalar";
        params.pixel_order = MB_BGR;
        unsigned char* one_shot = (unsigned char*)malloc((size_t)32 * stride);
        CHECK(one_shot != NULL);
        CHECK(mb_render_frame(&params, one_shot, stride, NULL, 0) == MB_OK);
        CHECK(memcmp(frame, one_shot, (size_t)32 * stride) == 0);
        free(one_shot);

        /* Iterations-only output matches the context render. */
        int32_t* one_shot_iterations = (int32_t*)malloc((size_t)32 * iteration_stride);
        CHECK(one_shot_iterations != NULL);
        CHECK(mb_render_frame(&params, NULL, 0, one_shot_iterations, iteration_stride) == MB_OK);
        CHECK(memcmp(iterations, one_shot_iterations, (size_t)32 * iteration_stride) == 0);
        free(one_shot_iterations);

        /* RGB output matches a context render in RGB order. */
        params.pixel_order = MB_RGB;
        unsigned char* one_shot_rgb = (unsigned char*)malloc((size_t)32 * stride);
        unsigned char* context_rgb = (unsigned char*)malloc((size_t)32 * stride);
        CHECK(one_shot_rgb != NULL && context_rgb != NULL);
        CHECK(mb_render_frame(&params, one_shot_rgb, stride, NULL, 0) == MB_OK);
        CHECK(mb_set_pixel_order(ctx, MB_RGB) == MB_OK);
        CHECK(mb_render_into(ctx, context_rgb, stride) == MB_OK);
        CHECK(memcmp(one_shot_rgb, context_rgb, (size_t)32 * stride) == 0);
        CHECK(mb_set_pixel_order(ctx, MB_BGR) == MB_OK);
        free(context_rgb);
        free(one_shot_rgb);
        params.pixel_order = MB_BGR;

        CHECK(mb_render_image(&params, "c_api_smoke_oneshot.bmp", MB_IMAGE_BMP) == MB_OK);
        remove("c_api_smoke_oneshot.bmp");

        /* Invalid one-shot parameters fail with MB_INVALID_ARGUMENT. */
        {
            mb_render_params bad = params;
            bad.zoom = 0.0;
            CHECK(mb_render_frame(&bad, NULL, 0, NULL, 0) == MB_INVALID_ARGUMENT);
            bad = params;
            bad.center_x = NAN;
            CHECK(mb_render_frame(&bad, NULL, 0, NULL, 0) == MB_INVALID_ARGUMENT);
            bad = params;
            bad.kernel = "bogus";
            CHECK(mb_render_frame(&bad, NULL, 0, NULL, 0) == MB_INVALID_ARGUMENT);
            bad = params;
            bad.pixel_order = (mb_pixel_order)9;
            CHECK(mb_render_frame(&bad, NULL, 0, NULL, 0) == MB_INVALID_ARGUMENT);
            bad = params;
            bad.width = 0;
            CHECK(mb_render_frame(&bad, NULL, 0, NULL, 0) == MB_INVALID_ARGUMENT);
            bad = params;
            bad.max_iterations = 1000001;
            CHECK(mb_render_frame(&bad, NULL, 0, NULL, 0) == MB_INVALID_ARGUMENT);

            CHECK(mb_render_image(&params, NULL, MB_IMAGE_BMP) == MB_INVALID_ARGUMENT);
            CHECK(mb_render_image(&params, "c_api_smoke_oneshot.bmp", (mb_image_format)9) == MB_INVALID_ARGUMENT);
            bad = params;
            bad.kernel = "bogus";
            CHECK(mb_render_image(&bad, "c_api_smoke_oneshot.bmp", MB_IMAGE_BMP) == MB_INVALID_ARGUMENT);
        }

        params.struct_size = 0;
        CHECK(mb_render_frame(&params, NULL, 0, NULL, 0) == MB_INVALID_ARGUMENT);
    }

    free(iterations);
    mb_destroy(ctx);
    mb_destroy(NULL);

    printf("c_api smoke OK (version %s)\n", mb_version());
    return 0;
}
