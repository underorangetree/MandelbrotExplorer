#include "mandelbrot/video.h"
#include <cmath>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include "Mandelbrot.h"
#include "MandelbrotOpenCV.h"
#include "VideoCodec.h"
#include "ViewSequence.h"

namespace {

thread_local std::string last_error;

// Thrown when the encoder cannot be opened; mapped to MB_VIDEO_ERROR so callers
// can distinguish "no usable video backend" from real failures.
struct VideoWriterError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <typename Function>
auto guard(Function&& function) -> mb_status {
    try {
        function();
        return MB_OK;
    } catch (const std::invalid_argument& error) {
        last_error = error.what();
        return MB_INVALID_ARGUMENT;
    } catch (const VideoWriterError& error) {
        last_error = error.what();
        return MB_VIDEO_ERROR;
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

auto require_params(const mb_video_params* params) -> bool {
    if (params == nullptr) {
        last_error = "params must not be null";
        return false;
    }
    if (params->struct_size < sizeof(mb_video_params)) {
        last_error = "mb_video_params.struct_size is too small";
        return false;
    }
    return true;
}

} // namespace

extern "C" {

const char* mb_video_last_error(void) {
    return last_error.c_str();
}

mb_status mb_write_video(const mb_video_params* params, const char* path) {
    if (!require_params(params)) {
        return MB_INVALID_ARGUMENT;
    }
    if (path == nullptr || path[0] == '\0') {
        last_error = "path must not be null or empty";
        return MB_INVALID_ARGUMENT;
    }
    if (!std::isfinite(params->fps) || params->fps <= 0.0) {
        last_error = "fps must be finite and positive";
        return MB_INVALID_ARGUMENT;
    }
    if (!std::isfinite(params->duration_seconds) || params->duration_seconds <= 0.0) {
        last_error = "duration_seconds must be finite and positive";
        return MB_INVALID_ARGUMENT;
    }
    const double total_frames = params->fps * params->duration_seconds;
    if (total_frames < 1.0) {
        last_error = "fps * duration must be at least 1 frame";
        return MB_INVALID_ARGUMENT;
    }
    const std::string codec = (params->codec != nullptr && params->codec[0] != '\0') ? params->codec : "mp4v";
    if (!mandelbrot::video_codec_known(codec)) {
        last_error = "unknown codec: " + codec;
        return MB_INVALID_ARGUMENT;
    }
    return guard([&] {
        Mandelbrot impl(params->width, params->height, params->max_iterations, params->threads);
        if (params->kernel != nullptr && !impl.set_kernel(params->kernel)) {
            throw std::invalid_argument(std::string("unknown or unsupported kernel: ") + params->kernel);
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
        cv::VideoWriter writer(path,
                               mandelbrot::video_codec_fourcc(codec),
                               params->fps,
                               cv::Size(params->width, params->height));
        if (!writer.isOpened()) {
            throw VideoWriterError("could not open the output video file (codec: " + codec + ")");
        }
        if (params->quality >= 0) {
            // Backend dependent; a codec that ignores the property is not an error.
            static_cast<void>(writer.set(cv::VIDEOWRITER_PROP_QUALITY, static_cast<double>(params->quality)));
        }
        for (int frame = 0; frame < total_frames; ++frame) {
            impl.setView(mandelbrot::animation_zoom(frame, total_frames),
                         mandelbrot::animation_center_x,
                         mandelbrot::animation_center_y);
            writer.write(generate_mat(impl));
        }
        writer.release();
    });
}

} // extern "C"
