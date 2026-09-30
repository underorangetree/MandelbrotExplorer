#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mandelbrot/mandelbrot.h"

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

    mb_destroy(ctx);
    mb_destroy(NULL);

    printf("c_api smoke OK (version %s)\n", mb_version());
    return 0;
}
