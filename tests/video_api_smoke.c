#include <stdio.h>
#include <string.h>
#include "mandelbrot_explorer/mandelbrot_explorer.h"
#include "mandelbrot_explorer/video.h"

int main(void) {
    mb_video_params params;
    memset(&params, 0, sizeof(params));
    params.struct_size = sizeof(params);
    params.width = 320;
    params.height = 240;
    params.max_iterations = 64;
    params.threads = 2;
    params.fps = 5.0;
    params.duration_seconds = 1.0;
    params.kernel = "scalar";
    params.pixel_order = MB_BGR;
    params.codec = "mp4v";
    params.quality = -1;

    /* Invalid parameters are rejected before touching the encoder. */
    if (mb_write_video(NULL, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
        return 1;
    }
    if (mb_write_video(&params, NULL) != MB_INVALID_ARGUMENT) {
        return 1;
    }
    {
        mb_video_params bad = params;
        bad.fps = 0.0;
        if (mb_write_video(&bad, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
            return 1;
        }
        bad = params;
        bad.duration_seconds = 0.1; /* fps * duration < 1 frame */
        if (mb_write_video(&bad, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
            return 1;
        }
        bad = params;
        bad.codec = "bogus";
        if (mb_write_video(&bad, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
            return 1;
        }
    }

    const mb_status status = mb_write_video(&params, "video_api_smoke.mp4");
    if (status == MB_VIDEO_ERROR) {
        fprintf(stderr, "no usable video backend: %s\n", mb_video_last_error());
        return 3; /* skipped, like the CLI video test */
    }
    if (status != MB_OK) {
        fprintf(stderr, "mb_write_video failed (%d): %s\n", (int)status, mb_video_last_error());
        return 1;
    }

    FILE* file = fopen("video_api_smoke.mp4", "rb");
    if (file == NULL) {
        return 1;
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fclose(file);
    remove("video_api_smoke.mp4");
    if (size <= 0) {
        return 1;
    }

    printf("video api smoke OK (%ld bytes)\n", size);
    return 0;
}
