#ifndef MANDELBROT_BUILD
#define MANDELBROT_BUILD
#endif
#include "mandelbrot/mandelbrot.h"
#include <cmath>
#include <exception>
#include <fstream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include "Mandelbrot.h"

#ifndef MANDELBROT_VERSION
#define MANDELBROT_VERSION "unknown"
#endif

struct mb_context {
    Mandelbrot impl;
    mb_context(int width, int height, int max_iterations, int threads)
        : impl(width, height, max_iterations, threads) {
    }
};

namespace {

thread_local std::string last_error;

template <typename Function>
auto guard(Function&& function) -> mb_status {
    try {
        function();
        return MB_OK;
    } catch (const std::invalid_argument& error) {
        last_error = error.what();
        return MB_INVALID_ARGUMENT;
    } catch (const std::bad_alloc&) {
        last_error = "memory allocation failed";
        return MB_INTERNAL_ERROR;
    } catch (const std::exception& error) {
        last_error = error.what();
        return MB_INTERNAL_ERROR;
    } catch (...) {
        last_error = "unknown error";
        return MB_INTERNAL_ERROR;
    }
}

auto require_context(const mb_context* ctx) -> bool {
    if (ctx == nullptr) {
        last_error = "context must not be null";
        return false;
    }
    return true;
}

template <typename Render>
auto render_and_report(mb_context* ctx, Render&& render) -> mb_status {
    ctx->impl.reset_cancel();
    const mb_status status = guard([&] { render(ctx->impl); });
    if (status != MB_OK) {
        return status;
    }
    return ctx->impl.is_cancelled() ? MB_CANCELLED : MB_OK;
}

void put_u16(std::uint8_t* out, std::uint32_t value) {
    out[0] = static_cast<std::uint8_t>(value & 0xFF);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void put_u32(std::uint8_t* out, std::uint32_t value) {
    put_u16(out, value);
    put_u16(out + 2, value >> 16);
}

// Copies `width` pixels of a row from `source` (stride in bytes) into `target`
// with optional red/blue swap (used to convert between BGR and RGB).
void copy_row(const std::uint8_t* source, std::uint8_t* target, int width, bool swap_red_blue) {
    for (int x = 0; x < width; ++x) {
        target[x * 3 + 0] = swap_red_blue ? source[x * 3 + 2] : source[x * 3 + 0];
        target[x * 3 + 1] = source[x * 3 + 1];
        target[x * 3 + 2] = swap_red_blue ? source[x * 3 + 0] : source[x * 3 + 2];
    }
}

auto write_pnm(const std::uint8_t* pixels, int width, int height, std::size_t stride,
               bool swap_red_blue, const char* path) -> bool {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file << "P6\n" << width << ' ' << height << "\n255\n";
    std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 3);
    for (int y = 0; y < height; ++y) {
        copy_row(pixels + static_cast<std::size_t>(y) * stride, row.data(), width, swap_red_blue);
        file.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(file);
}

auto write_bmp(const std::uint8_t* pixels, int width, int height, std::size_t stride,
               bool swap_red_blue, const char* path) -> bool {
    const int row_size = ((width * 3 + 3) / 4) * 4; // rows are padded to 4 bytes
    const std::uint32_t image_size = static_cast<std::uint32_t>(row_size) * static_cast<std::uint32_t>(height);
    std::uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    put_u32(header + 2, 54 + image_size);
    put_u32(header + 10, 54);
    put_u32(header + 14, 40);
    put_u32(header + 18, static_cast<std::uint32_t>(width));
    put_u32(header + 22, static_cast<std::uint32_t>(height)); // positive: bottom-up
    put_u16(header + 26, 1);
    put_u16(header + 28, 24);
    put_u32(header + 34, image_size);
    put_u32(header + 38, 2835); // 72 dpi
    put_u32(header + 42, 2835);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    std::vector<std::uint8_t> row(static_cast<std::size_t>(row_size), 0);
    for (int y = height - 1; y >= 0; --y) {
        copy_row(pixels + static_cast<std::size_t>(y) * stride, row.data(), width, swap_red_blue);
        file.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(file);
}

void apply_params(Mandelbrot& impl, const mb_render_params* params) {
    if (!std::isfinite(params->zoom) || params->zoom <= 0.0 ||
        !std::isfinite(params->center_x) || !std::isfinite(params->center_y)) {
        throw std::invalid_argument("zoom must be finite and positive, center must be finite");
    }
    switch (params->pixel_order) {
        case MB_BGR:
            impl.set_pixel_order(Mandelbrot::PixelOrder::Bgr);
            break;
        case MB_RGB:
            impl.set_pixel_order(Mandelbrot::PixelOrder::Rgb);
            break;
        default:
            throw std::invalid_argument("unknown pixel order");
    }
    if (params->kernel != nullptr && !impl.set_kernel(params->kernel)) {
        throw std::invalid_argument(std::string("unknown or unsupported kernel: ") + params->kernel);
    }
    impl.setView(params->zoom, params->center_x, params->center_y);
}

auto require_params(const mb_render_params* params) -> bool {
    if (params == nullptr) {
        last_error = "params must not be null";
        return false;
    }
    if (params->struct_size < sizeof(mb_render_params)) {
        last_error = "mb_render_params.struct_size is too small";
        return false;
    }
    return true;
}

} // namespace

extern "C" {

const char* mb_version(void) {
    return MANDELBROT_VERSION;
}

const char* mb_last_error(void) {
    return last_error.c_str();
}

mb_context* mb_create(int width, int height, int max_iterations, int threads) {
    mb_context* ctx = nullptr;
    const mb_status status = guard([&] { ctx = new mb_context(width, height, max_iterations, threads); });
    if (status != MB_OK) {
        return nullptr;
    }
    return ctx;
}

void mb_destroy(mb_context* ctx) {
    delete ctx;
}

int mb_width(const mb_context* ctx) {
    return ctx != nullptr ? ctx->impl.frame_width() : 0;
}

int mb_height(const mb_context* ctx) {
    return ctx != nullptr ? ctx->impl.frame_height() : 0;
}

int mb_storage_width(const mb_context* ctx) {
    return ctx != nullptr ? ctx->impl.storage_width() : 0;
}

int mb_thread_count(const mb_context* ctx) {
    return ctx != nullptr ? ctx->impl.thread_count() : 0;
}

const char* mb_kernel_name(const mb_context* ctx) {
    return ctx != nullptr ? ctx->impl.kernel_name() : nullptr;
}

mb_status mb_set_view(mb_context* ctx, double zoom, double center_x, double center_y) {
    if (!require_context(ctx)) {
        return MB_INVALID_ARGUMENT;
    }
    if (!std::isfinite(zoom) || zoom <= 0.0 || !std::isfinite(center_x) || !std::isfinite(center_y)) {
        last_error = "zoom must be finite and positive, center must be finite";
        return MB_INVALID_ARGUMENT;
    }
    ctx->impl.setView(zoom, center_x, center_y);
    return MB_OK;
}

mb_status mb_set_kernel(mb_context* ctx, const char* name) {
    if (!require_context(ctx)) {
        return MB_INVALID_ARGUMENT;
    }
    if (name == nullptr) {
        last_error = "kernel name must not be null";
        return MB_INVALID_ARGUMENT;
    }
    if (!ctx->impl.set_kernel(name)) {
        last_error = std::string("unknown or unsupported kernel: ") + name;
        return MB_INVALID_ARGUMENT;
    }
    return MB_OK;
}

mb_status mb_set_pixel_order(mb_context* ctx, mb_pixel_order order) {
    if (!require_context(ctx)) {
        return MB_INVALID_ARGUMENT;
    }
    switch (order) {
        case MB_BGR:
            ctx->impl.set_pixel_order(Mandelbrot::PixelOrder::Bgr);
            return MB_OK;
        case MB_RGB:
            ctx->impl.set_pixel_order(Mandelbrot::PixelOrder::Rgb);
            return MB_OK;
        default:
            last_error = "unknown pixel order";
            return MB_INVALID_ARGUMENT;
    }
}

mb_status mb_render(mb_context* ctx) {
    if (!require_context(ctx)) {
        return MB_INVALID_ARGUMENT;
    }
    return render_and_report(ctx, [](Mandelbrot& impl) { static_cast<void>(impl.generate()); });
}

const unsigned char* mb_frame_data(const mb_context* ctx) {
    return ctx != nullptr ? ctx->impl.frame_data() : nullptr;
}

mb_status mb_render_into(mb_context* ctx, unsigned char* bgr, size_t stride) {
    if (!require_context(ctx)) {
        return MB_INVALID_ARGUMENT;
    }
    if (bgr == nullptr) {
        last_error = "frame buffer must not be null";
        return MB_INVALID_ARGUMENT;
    }
    return render_and_report(ctx, [bgr, stride](Mandelbrot& impl) { impl.generate_into(bgr, stride); });
}

mb_status mb_render_outputs(mb_context* ctx, unsigned char* bgr, size_t bgr_stride,
                            int32_t* iterations, size_t iterations_stride) {
    if (!require_context(ctx)) {
        return MB_INVALID_ARGUMENT;
    }
    return render_and_report(ctx, [=](Mandelbrot& impl) {
        impl.generate_frame({.bgr=bgr,
                             .bgr_stride=bgr_stride,
                             .iterations=iterations,
                             .iterations_stride=iterations_stride});
    });
}

mb_status mb_write_image(mb_context* ctx, const char* path, mb_image_format format) {
    if (!require_context(ctx)) {
        return MB_INVALID_ARGUMENT;
    }
    if (path == nullptr) {
        last_error = "path must not be null";
        return MB_INVALID_ARGUMENT;
    }
    const mb_status status = mb_render(ctx);
    if (status != MB_OK) {
        return status;
    }
    const bool bgr_order = ctx->impl.pixel_order() == Mandelbrot::PixelOrder::Bgr;
    const std::uint8_t* const pixels = ctx->impl.frame_data();
    const int width = ctx->impl.frame_width();
    const int height = ctx->impl.frame_height();
    const std::size_t stride = static_cast<std::size_t>(ctx->impl.storage_width()) * 3;
    bool ok = false;
    switch (format) {
        case MB_IMAGE_BMP:
            ok = write_bmp(pixels, width, height, stride, !bgr_order, path); // BMP stores BGR
            break;
        case MB_IMAGE_PNM:
            ok = write_pnm(pixels, width, height, stride, bgr_order, path); // PNM stores RGB
            break;
        default:
            last_error = "unknown image format";
            return MB_INVALID_ARGUMENT;
    }
    if (!ok) {
        last_error = std::string("could not write image: ") + path;
        return MB_INTERNAL_ERROR;
    }
    return MB_OK;
}

void mb_cancel(mb_context* ctx) {
    if (ctx != nullptr) {
        ctx->impl.request_cancel();
    }
}

mb_status mb_render_frame(const mb_render_params* params, unsigned char* bgr, size_t bgr_stride,
                          int32_t* iterations, size_t iterations_stride) {
    if (!require_params(params)) {
        return MB_INVALID_ARGUMENT;
    }
    return guard([&] {
        Mandelbrot impl(params->width, params->height, params->max_iterations, params->threads);
        apply_params(impl, params);
        impl.generate_frame({.bgr=bgr,
                             .bgr_stride=bgr_stride,
                             .iterations=iterations,
                             .iterations_stride=iterations_stride});
    });
}

mb_status mb_render_image(const mb_render_params* params, const char* path, mb_image_format format) {
    if (!require_params(params)) {
        return MB_INVALID_ARGUMENT;
    }
    if (path == nullptr) {
        last_error = "path must not be null";
        return MB_INVALID_ARGUMENT;
    }
    mb_context* ctx = nullptr;
    const mb_status status = guard([&] {
        ctx = new mb_context(params->width, params->height, params->max_iterations, params->threads);
        apply_params(ctx->impl, params);
    });
    if (status != MB_OK) {
        delete ctx; // null-safe
        return status;
    }
    const mb_status image_status = mb_write_image(ctx, path, format);
    delete ctx;
    return image_status;
}

} // extern "C"
