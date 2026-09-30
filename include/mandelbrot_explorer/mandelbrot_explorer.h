/* C API for MandelbrotExplorer.
 *
 * The library renders BGR8 frames. A context owns its own worker pool and is
 * not thread safe: use one context per thread and call into a given context
 * from a single thread at a time. Cancellation is the only exception and may be
 * requested from any thread.
 */
#ifndef MANDELBROT_EXPLORER_H
#define MANDELBROT_EXPLORER_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(MANDELBROT_EXPLORER_BUILD)
#    define MB_API __declspec(dllexport)
#  else
#    define MB_API __declspec(dllimport)
#  endif
#else
#  define MB_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mb_context mb_context;

typedef enum mb_status {
    MB_OK = 0,
    MB_INVALID_ARGUMENT = 1,
    MB_INTERNAL_ERROR = 2,
    MB_CANCELLED = 3,
    MB_VIDEO_ERROR = 4 /* the video writer could not be opened (video API) */
} mb_status;

/* Channel order of the rendered color frames. BGR matches OpenCV/FFmpeg,
 * RGB matches numpy/PIL conventions. In C++ the enums get a fixed underlying
 * type: a C caller may pass any integer, and reading an out-of-range value
 * must not be undefined behavior (the implementation rejects it). */
#ifdef __cplusplus
typedef enum mb_pixel_order : int {
#else
typedef enum mb_pixel_order {
#endif
    MB_BGR = 0,
    MB_RGB = 1
} mb_pixel_order;

#ifdef __cplusplus
typedef enum mb_image_format : int {
#else
typedef enum mb_image_format {
#endif
    MB_IMAGE_BMP = 0,
    MB_IMAGE_PNM = 1
} mb_image_format;

/* Library version, e.g. "0.5.0". Never NULL. */
MB_API const char* mb_version(void);

/* Message of the last failed call on this thread. Never NULL. */
MB_API const char* mb_last_error(void);

/* Creates a context; width/height/max_iterations are validated by the core
 * (std::invalid_argument -> MB_INVALID_ARGUMENT) and `threads` 0 selects
 * hardware_concurrency(). Returns NULL on failure (see mb_last_error). */
MB_API mb_context* mb_create(int width, int height, int max_iterations, int threads);

/* Frees the context and joins its workers. Safe with NULL. */
MB_API void mb_destroy(mb_context* ctx);

/* Requested (cropped) width / height of a frame. */
MB_API int mb_width(const mb_context* ctx);
MB_API int mb_height(const mb_context* ctx);

/* Padded storage width: rows hold storage_width() * 3 bytes (the kernels render
 * whole 32-column blocks). Zero for a NULL context. */
MB_API int mb_storage_width(const mb_context* ctx);

/* Number of render worker threads. Zero for a NULL context. */
MB_API int mb_thread_count(const mb_context* ctx);

/* Name of the selected kernel ("scalar", "avx2", ...). NULL for a NULL context. */
MB_API const char* mb_kernel_name(const mb_context* ctx);

/* Sets the view: pixel size = 4 / max(width, height) / zoom around (center_x,
 * center_y). zoom must be finite and positive. */
MB_API mb_status mb_set_view(mb_context* ctx, double zoom, double center_x, double center_y);

/* Forces a kernel: auto|scalar|avx2|avx512|neon. Returns MB_INVALID_ARGUMENT
 * when the name is unknown or the CPU does not support it. */
MB_API mb_status mb_set_kernel(mb_context* ctx, const char* name);

/* Rebuilds the color map in the requested channel order; affects subsequent
 * renders only. */
MB_API mb_status mb_set_pixel_order(mb_context* ctx, mb_pixel_order order);

/* Renders into the internal buffer. mb_frame_data() then points to
 * `height` rows of BGR8 pixels with stride 3 * mb_storage_width(); only the
 * first mb_width() columns are meaningful. The pointer stays valid until the
 * next render or mb_destroy. */
MB_API mb_status mb_render(mb_context* ctx);
MB_API const unsigned char* mb_frame_data(const mb_context* ctx);

/* Renders into a caller-owned BGR8 buffer. `stride` is in bytes and must be at
 * least 3 * mb_storage_width(); the buffer must stay untouched until the call
 * returns. */
MB_API mb_status mb_render_into(mb_context* ctx, unsigned char* bgr, size_t stride);

/* Renders into the requested outputs: colors (bgr, stride in bytes) and/or
 * per-pixel iteration counts (int32, stride in bytes). At least one output must
 * be set; the buffers must stay untouched until the call returns. */
MB_API mb_status mb_render_outputs(mb_context* ctx,
                                   unsigned char* bgr, size_t bgr_stride,
                                   int32_t* iterations, size_t iterations_stride);

/* Renders the current view and writes it to `path` as BMP or PNM (P6). */
MB_API mb_status mb_write_image(mb_context* ctx, const char* path, mb_image_format format);

/* Requests cancellation of a render in progress; the render call returns
 * MB_CANCELLED with a partially rendered frame. The flag is sticky and is
 * cleared at the beginning of the next render. Thread-safe. */
MB_API void mb_cancel(mb_context* ctx);

/* One-shot render parameters. Set struct_size to sizeof(mb_render_params) so
 * future fields can be added safely. The context-based API is a better fit for
 * animation sequences (it keeps the worker pool alive). */
typedef struct mb_render_params {
    size_t struct_size;
    int width;
    int height;
    int max_iterations;
    int threads;              /* 0 = auto */
    double zoom;
    double center_x;
    double center_y;
    const char* kernel;       /* NULL = auto */
    mb_pixel_order pixel_order;
} mb_render_params;

/* Renders one frame with the given parameters into the outputs. */
MB_API mb_status mb_render_frame(const mb_render_params* params,
                                 unsigned char* bgr, size_t bgr_stride,
                                 int32_t* iterations, size_t iterations_stride);

/* Renders one frame with the given parameters and writes it to `path`. */
MB_API mb_status mb_render_image(const mb_render_params* params, const char* path, mb_image_format format);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MANDELBROT_EXPLORER_H */
