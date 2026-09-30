#ifndef MANDELBROT_BUILD
#define MANDELBROT_BUILD
#endif
#include "mandelbrot/mandelbrot.h"
#include <cmath>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
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

void mb_cancel(mb_context* ctx) {
    if (ctx != nullptr) {
        ctx->impl.request_cancel();
    }
}

} // extern "C"
