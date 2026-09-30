#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <opencv2/core.hpp>
#include "Mandelbrot.h"

// OpenCV adapters for the OpenCV-free core. The returned Mat is a view over the
// library (or caller) buffer, never owned by it; the caller must not render
// again while holding the view.

// Renders and returns the requested-width ROI of the internal buffer (the ROI
// can be non-continuous because the storage is padded to a 32-column multiple).
[[nodiscard]] inline auto generate_mat(Mandelbrot& mandelbrot) -> cv::Mat {
    uint8_t* data = const_cast<uint8_t*>(mandelbrot.generate());
    cv::Mat storage(mandelbrot.frame_height(), mandelbrot.storage_width(), CV_8UC3, data);
    return storage(cv::Rect(0, 0, mandelbrot.frame_width(), mandelbrot.frame_height()));
}

// Renders into a caller-provided CV_8UC3 target with `height` rows and at least
// `storage_width()` columns.
inline void generate_into_mat(Mandelbrot& mandelbrot, cv::Mat& target) {
    if (target.type() != CV_8UC3 || target.rows != mandelbrot.frame_height() ||
        target.cols < mandelbrot.storage_width()) {
        throw std::invalid_argument("frame buffer must be CV_8UC3 with " + std::to_string(mandelbrot.frame_height()) +
                                    " rows and at least " + std::to_string(mandelbrot.storage_width()) + " columns");
    }
    mandelbrot.generate_into(target.data, target.step);
}
