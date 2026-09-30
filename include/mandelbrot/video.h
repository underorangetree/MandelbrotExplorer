/* Video export API (zoom animation -> video file).
 *
 * This API lives in the OpenCV-based `mandelbrot_video` shared library so the
 * core `mandelbrot` library stays dependency-free. Build one target with the
 * application's zoom animation (see src/ViewSequence.h) and encode it with the
 * configured codec (see src/VideoCodec.h).
 */
#ifndef MANDELBROT_VIDEO_H
#define MANDELBROT_VIDEO_H

#include <stddef.h>
#include "mandelbrot/mandelbrot.h"

#if defined(_WIN32)
#  if defined(MANDELBROT_VIDEO_BUILD)
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
 * so future fields can be added safely. The animation and the accepted codec
 * names match the command line application. */
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

#endif /* MANDELBROT_VIDEO_H */
