#include <math.h>
#include <stddef.h>
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
    if (mb_write_video(&params, "") != MB_INVALID_ARGUMENT) {
        return 1;
    }
    {
        mb_video_params bad = params;
        bad.fps = 0.0;
        if (mb_write_video(&bad, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
            return 1;
        }
        bad = params;
        bad.fps = NAN;
        if (mb_write_video(&bad, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
            return 1;
        }
        bad = params;
        bad.duration_seconds = 0.1; /* fps * duration < 1 frame */
        if (mb_write_video(&bad, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
            return 1;
        }
        bad = params;
        bad.duration_seconds = NAN;
        if (mb_write_video(&bad, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
            return 1;
        }
        bad = params;
        bad.codec = "bogus";
        if (mb_write_video(&bad, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
            return 1;
        }
        bad = params;
        bad.kernel = "bogus";
        if (mb_write_video(&bad, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
            return 1;
        }
        bad = params;
        bad.pixel_order = (mb_pixel_order)9;
        if (mb_write_video(&bad, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
            return 1;
        }
        bad = params;
        bad.struct_size = 0;
        if (mb_write_video(&bad, "video_api_smoke.mp4") != MB_INVALID_ARGUMENT) {
            return 1;
        }
    }
    if (mb_video_last_error() == NULL || mb_video_last_error()[0] == '\0') {
        fprintf(stderr, "a failed call must leave an error message behind\n");
        return 1;
    }

    /* An unwritable path fails while opening the encoder (MB_VIDEO_ERROR, not
     * a crash); this is not the "no backend" skip path. */
    if (mb_write_video(&params, "no_such_directory/video_api_smoke.mp4") != MB_VIDEO_ERROR) {
        fprintf(stderr, "an unwritable path must report MB_VIDEO_ERROR\n");
        return 1;
    }

    /* The main write covers the default codec (NULL) and the quality path. */
    params.codec = NULL;
    params.quality = 50;
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

    /* Custom view: the appended fields are read when struct_size covers the
     * whole struct (these run after the backend check, so a missing encoder
     * still only skips the test). */
    {
        mb_video_params custom = params;
        custom.center_x = -0.5;
        custom.center_y = 0.0;
        custom.start_zoom = 1.0;
        custom.end_zoom = 2.0;
        if (mb_write_video(&custom, "video_api_smoke_view.mp4") != MB_OK) {
            return 1;
        }
        remove("video_api_smoke_view.mp4");

        /* start_zoom == end_zoom renders a static view. */
        custom.start_zoom = 1.5;
        custom.end_zoom = 1.5;
        if (mb_write_video(&custom, "video_api_smoke_static.mp4") != MB_OK) {
            return 1;
        }
        remove("video_api_smoke_static.mp4");
    }

    /* A struct sized like the original 0.6.0 layout keeps every default. */
    {
        mb_video_params old;
        memset(&old, 0, sizeof(old));
        old.struct_size = offsetof(mb_video_params, center_x);
        old.width = 160;
        old.height = 120;
        old.max_iterations = 8;
        old.threads = 1;
        old.fps = 2.0;
        old.duration_seconds = 1.0;
        old.kernel = "scalar";
        old.pixel_order = MB_BGR;
        if (mb_write_video(&old, "video_api_smoke_old.mp4") != MB_OK) {
            return 1;
        }
        remove("video_api_smoke_old.mp4");
    }

    /* RGB output with the encoder-default quality. */
    {
        mb_video_params rgb = params;
        rgb.width = 160;
        rgb.height = 120;
        rgb.max_iterations = 8;
        rgb.pixel_order = MB_RGB;
        rgb.quality = -1;
        if (mb_write_video(&rgb, "video_api_smoke_rgb.mp4") != MB_OK) {
            return 1;
        }
        remove("video_api_smoke_rgb.mp4");
    }

    printf("video api smoke OK (%ld bytes)\n", size);
    return 0;
}
