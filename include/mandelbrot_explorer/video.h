/* Video export API (zoom animation -> video file).
 *
 * This API lives in the OpenCV-based `mandelbrot_explorer_video` shared library so the
 * core `mandelbrot` library stays dependency-free. Build one target with the
 * application's zoom animation (see src/ViewSequence.h) and encode it with the
 * configured codec (see src/VideoCodec.h).
 */
#ifndef MANDELBROT_EXPLORER_VIDEO_H
#define MANDELBROT_EXPLORER_VIDEO_H

#include <stddef.h>
#include "mandelbrot_explorer/mandelbrot_explorer.h"

#if defined(_WIN32)
#  if defined(MANDELBROT_EXPLORER_VIDEO_BUILD)
#    define MB_VIDEO_API __declspec(dllexport)
#  else
#    define MB_VIDEO_API __declspec(dllimport)
#  endif
#else
#  define MB_VIDEO_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* One-shot video export parameters. Set struct_size to sizeof(mb_video_params)
 * so future fields can be added safely: callers compiled against an older
 * header may pass a smaller struct_size, and the fields their struct does not
 * cover fall back to the defaults (the command line application's animation).
 * The accepted codec names also match the application. */
typedef struct mb_video_params {
    size_t struct_size;
    int width;
    int height;
    int max_iterations;
    int threads;               /* 0 = auto */
    double fps;
    double duration_seconds;   /* fps * duration must be at least 1 frame */
    const char* kernel;        /* NULL = auto (scalar|avx2|avx512|neon) */
    mb_pixel_order pixel_order;
    const char* codec;         /* NULL = "mp4v" (mp4v|h264|h265|vp9|av1|mjpg) */
    int quality;               /* 0..100, -1 = encoder default */
    /* Custom view (appended in 0.7.0; defaults reproduce the application's
     * animation). All four fields are read only when struct_size covers the
     * whole struct, so older callers keep the defaults. */
    double center_x;           /* animation center, finite */
    double center_y;
    double start_zoom;         /* zoom of the first frame; <= 0 = default (0.8) */
    double end_zoom;           /* zoom of the last frame; <= 0 keeps the default
                                * depth factor (start_zoom * 1.05^280).
                                * start_zoom == end_zoom renders a static view */
} mb_video_params;

/* Renders the animation and writes it to `path`. Returns MB_VIDEO_ERROR when
 * the encoder cannot be opened (for example when OpenCV has no usable video
 * backend); the message is available through mb_video_last_error(). */
MB_VIDEO_API mb_status mb_write_video(const mb_video_params* params, const char* path);

/* Message of the last failed mb_write_video() call on this thread. Never NULL. */
MB_VIDEO_API const char* mb_video_last_error(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MANDELBROT_EXPLORER_VIDEO_H */
