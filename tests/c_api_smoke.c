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

int main(void) {
    CHECK(mb_version() != NULL);
    CHECK(mb_last_error() != NULL);

    /* Invalid arguments are reported through the status/error API. */
    CHECK(mb_create(0, 0, 0, 0) == NULL);
    CHECK(mb_last_error()[0] != '\0');

    mb_context* ctx = mb_create(64, 32, 64, 2);
    CHECK(ctx != NULL);
    CHECK(mb_width(ctx) == 64);
    CHECK(mb_height(ctx) == 32);
    CHECK(mb_storage_width(ctx) >= 64);
    CHECK(mb_thread_count(ctx) == 2);
    CHECK(mb_kernel_name(ctx) != NULL);

    CHECK(mb_set_view(ctx, 1.0, -0.5, 0.0) == MB_OK);
    CHECK(mb_set_view(ctx, 0.0, 0.0, 0.0) == MB_INVALID_ARGUMENT);
    CHECK(mb_set_kernel(ctx, "scalar") == MB_OK);
    CHECK(strcmp(mb_kernel_name(ctx), "scalar") == 0);
    CHECK(mb_set_kernel(ctx, "bogus") == MB_INVALID_ARGUMENT);

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

    /* Image files (dependency-free BMP / PNM writers). */
    CHECK(mb_write_image(ctx, "c_api_smoke.bmp", MB_IMAGE_BMP) == MB_OK);
    CHECK(mb_write_image(ctx, "c_api_smoke.ppm", MB_IMAGE_PNM) == MB_OK);
    {
        unsigned char magic[2] = {0, 0};
        FILE* file = fopen("c_api_smoke.bmp", "rb");
        CHECK(file != NULL);
        CHECK(fread(magic, 1, 2, file) == 2);
        fclose(file);
        CHECK(magic[0] == 'B' && magic[1] == 'M');
        file = fopen("c_api_smoke.ppm", "rb");
        CHECK(file != NULL);
        CHECK(fread(magic, 1, 2, file) == 2);
        fclose(file);
        CHECK(magic[0] == 'P' && magic[1] == '6');
        remove("c_api_smoke.bmp");
        remove("c_api_smoke.ppm");
    }

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
        CHECK(mb_render_image(&params, "c_api_smoke_oneshot.bmp", MB_IMAGE_BMP) == MB_OK);
        remove("c_api_smoke_oneshot.bmp");

        params.struct_size = 0;
        CHECK(mb_render_frame(&params, NULL, 0, NULL, 0) == MB_INVALID_ARGUMENT);
    }

    free(iterations);
    mb_destroy(ctx);
    mb_destroy(NULL);

    printf("c_api smoke OK (version %s)\n", mb_version());
    return 0;
}
