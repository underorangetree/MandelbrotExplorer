#include "Mandelbrot.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include "MandelbrotLimits.h"

namespace {

auto validate_dimension(int value, const char* name) -> int {
    if (value < mandelbrot::min_dimension || value > mandelbrot::max_dimension) {
        throw std::invalid_argument(std::string(name) + " must be in [" +
                                    std::to_string(mandelbrot::min_dimension) + ", " +
                                    std::to_string(mandelbrot::max_dimension) + "], got " + std::to_string(value));
    }
    return value;
}

auto validate_max_iterations(int value) -> int {
    if (value < mandelbrot::min_iteration_count || value > mandelbrot::max_iteration_count) {
        throw std::invalid_argument("max_iterations must be in [" +
                                    std::to_string(mandelbrot::min_iteration_count) + ", " +
                                    std::to_string(mandelbrot::max_iteration_count) + "], got " + std::to_string(value));
    }
    return value;
}

auto padded_width(int value) -> int {
    return (value + 7) & ~7; // pad to a multiple of 8 so any kernel fits
}

} // namespace

Mandelbrot::Mandelbrot(int _width, int _height, int _maxIterations):
    width(padded_width(validate_dimension(_width, "width"))),
    requested_width(_width),
    height(validate_dimension(_height, "height")),
    max_iterations(validate_max_iterations(_maxIterations)),
    r_data(static_cast<double*>(operator new(static_cast<size_t>(width) * sizeof(double), std::align_val_t(32)))),
    i_data(static_cast<double*>(operator new(static_cast<size_t>(height) * sizeof(double), std::align_val_t(32)))),
    thread_pool(height),
    image(height, width, CV_8UC3) {
    if (!image.isContinuous()) {
        throw std::runtime_error("cv::Mat image is not continuous");
    }
    const KernelSelection selection = select_kernel();
    kernel_ = selection.function;
    kernel_name_ = selection.name;
    setView(zoom, offset_x, offset_y);
    color_map.reserve(static_cast<size_t>(max_iterations) + 1);
    const float iter_scale = 4.0F;
    const float min_kelvin = 100.0F;
    const float max_kelvin = 6000.0F;
    for (int iter = 0; iter < max_iterations; ++iter) {
        const float kelvin = std::min(min_kelvin + (iter_scale * static_cast<float>(iter)), max_kelvin);
        auto clamp = [](float value) -> uint8_t {
            return static_cast<uint8_t>(std::clamp(value, 0.0F, 255.0F));
        };
        const float b = 51 * iter_scale * kelvin * (kelvin - 2000) / 800 / max_kelvin;
        const float g = 51 * iter_scale * kelvin * (kelvin - 500) / 1100 / max_kelvin;
        const float r = 255 * iter_scale * kelvin / max_kelvin;
        color_map.push_back({clamp(b), clamp(g), clamp(r)});
    }
    color_map.push_back({0, 0, 0});
}

void Mandelbrot::setView(double new_zoom, double new_offset_x, double new_offset_y) {
    zoom = new_zoom;
    delta = 4.0 / std::max(requested_width, height) / zoom;
    offset_x = new_offset_x;
    offset_y = new_offset_y;
    for (int col = 0; col < width; ++col) {
        r_data.get()[col] = std::fma(col - (requested_width / 2), delta, offset_x);
    }
    for (int row = 0; row < height; ++row) {
        i_data.get()[row] = std::fma(row - (height / 2), delta, offset_y);
    }
}

void Mandelbrot::render_tile(int row_index, int x_begin, int x_end) {
    const RowContext context{
        r_data.get(),
        i_data.get()[row_index],
        image.data + (static_cast<size_t>(row_index) * image.step),
        x_begin,
        x_end,
        max_iterations,
        color_map.data()
    };
    kernel_(context);
}

auto Mandelbrot::generate() -> cv::Mat {
    if (schedule_ == Schedule::PerRow) {
        for (int row_index = 0; row_index < height; ++row_index) {
            thread_pool.enqueue([this, row_index]() { render_tile(row_index, 0, width); });
        }
        thread_pool.wait_all_idle();
        return image(cv::Rect(0, 0, requested_width, height));
    }

    // Row-based work stealing: the unit of work is one full row and workers pull
    // the next row from an atomic counter. Enqueuing only as many tasks as there
    // are workers removes the per-row enqueue overhead while preserving the
    // sequential row access pattern (good locality for r_data and the image).
    std::atomic<int> next_row{0};
    const int workers = thread_pool.thread_count();
    for (int worker = 0; worker < workers; ++worker) {
        thread_pool.enqueue([this, &next_row]() {
            for (;;) {
                const int row_index = next_row.fetch_add(1, std::memory_order_relaxed);
                if (row_index >= height) {
                    return;
                }
                render_tile(row_index, 0, width);
            }
        });
    }
    thread_pool.wait_all_idle();
    return image(cv::Rect(0, 0, requested_width, height));
}

auto Mandelbrot::kernel_name() const -> const char* {
    return kernel_name_;
}
