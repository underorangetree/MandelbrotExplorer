#include "Mandelbrot.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
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

auto validate_thread_count(int value) -> int {
    if (value < 0 || value > mandelbrot::max_thread_count) {
        throw std::invalid_argument("threads must be in [0, " +
                                    std::to_string(mandelbrot::max_thread_count) + "], got " + std::to_string(value));
    }
    return value;
}

auto padded_width(int value) -> int {
    return (value + 31) & ~31; // pad to a multiple of 32 so the widest (AVX-512) kernel fits
}

} // namespace

Mandelbrot::Mandelbrot(int _width, int _height, int _maxIterations, int _threads):
    width(padded_width(validate_dimension(_width, "width"))),
    requested_width(_width),
    height(validate_dimension(_height, "height")),
    max_iterations(validate_max_iterations(_maxIterations)),
    r_data(static_cast<double*>(operator new(static_cast<size_t>(width) * sizeof(double), std::align_val_t(32)))),
    i_data(static_cast<double*>(operator new(static_cast<size_t>(height) * sizeof(double), std::align_val_t(32)))),
    thread_pool(height, validate_thread_count(_threads)),
    image(static_cast<size_t>(height) * width * 3) {
    color_output_ = image.data();
    color_step_ = static_cast<size_t>(width) * 3;
    const KernelSelection selection = select_kernel();
    kernel_ = selection.function;
    kernel_name_ = selection.name;
    setView(zoom, offset_x, offset_y);
    rebuild_color_map();
}

void Mandelbrot::rebuild_color_map() {
    color_map.clear();
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
        if (pixel_order_ == PixelOrder::Rgb) {
            color_map.push_back({clamp(r), clamp(g), clamp(b)});
        } else {
            color_map.push_back({clamp(b), clamp(g), clamp(r)});
        }
    }
    color_map.push_back({0, 0, 0});
}

void Mandelbrot::set_pixel_order(PixelOrder order) {
    if (pixel_order_ == order) {
        return;
    }
    pixel_order_ = order;
    rebuild_color_map();
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
        color_output_ != nullptr
            ? color_output_ + (static_cast<size_t>(row_index) * color_step_)
            : nullptr,
        x_begin,
        x_end,
        max_iterations,
        color_map.data(),
        iteration_output_ != nullptr
            ? reinterpret_cast<std::int32_t*>(
                  iteration_output_ + (static_cast<size_t>(row_index) * iteration_step_))
            : nullptr,
        iteration_step_
    };
    kernel_(context);
}

auto Mandelbrot::generate() -> const uint8_t* {
    generate_frame({.bgr=image.data(), .bgr_stride=static_cast<size_t>(width) * 3});
    return image.data();
}

auto Mandelbrot::generate_into(uint8_t* buffer, std::size_t stride) -> void {
    generate_frame({.bgr=buffer, .bgr_stride=stride});
}

auto Mandelbrot::generate_frame(const FrameTargets& targets) -> void {
    if (targets.bgr == nullptr && targets.iterations == nullptr) {
        throw std::invalid_argument("at least one output buffer is required");
    }
    if (targets.bgr != nullptr && targets.bgr_stride < static_cast<size_t>(width) * 3) {
        throw std::invalid_argument("color buffer must provide " + std::to_string(height) +
                                    " rows of at least " + std::to_string(width) + " pixels");
    }
    if (targets.iterations != nullptr &&
        targets.iterations_stride < static_cast<size_t>(width) * sizeof(std::int32_t)) {
        throw std::invalid_argument("iteration buffer must provide " + std::to_string(height) +
                                    " rows of at least " + std::to_string(width) + " int32 values");
    }
    color_output_ = targets.bgr;
    color_step_ = targets.bgr_stride;
    iteration_output_ = reinterpret_cast<uint8_t*>(targets.iterations);
    iteration_step_ = targets.iterations_stride;
    if (schedule_ == Schedule::PerRow) {
        for (int row_index = 0; row_index < height && !is_cancelled(); ++row_index) {
            thread_pool.enqueue([this, row_index]() {
                if (!is_cancelled()) {
                    render_tile(row_index, 0, width);
                }
            });
        }
        thread_pool.wait_all_idle();
    } else {
        // Row-based work stealing: the unit of work is one full row and workers
        // pull the next row from an atomic counter. Enqueuing only as many tasks
        // as there are workers removes the per-row enqueue overhead while
        // preserving the sequential row access pattern (good locality for r_data
        // and the image).
        std::atomic<int> next_row{0};
        const int workers = thread_pool.thread_count();
        for (int worker = 0; worker < workers; ++worker) {
            thread_pool.enqueue([this, &next_row]() {
                for (;;) {
                    if (is_cancelled()) {
                        return;
                    }
                    const int row_index = next_row.fetch_add(1, std::memory_order_relaxed);
                    if (row_index >= height) {
                        return;
                    }
                    render_tile(row_index, 0, width);
                }
            });
        }
        thread_pool.wait_all_idle();
    }
    color_output_ = image.data();
    color_step_ = static_cast<size_t>(width) * 3;
    iteration_output_ = nullptr;
    iteration_step_ = 0;
}

auto Mandelbrot::kernel_name() const -> const char* {
    return kernel_name_;
}

auto Mandelbrot::thread_count() const -> int {
    return thread_pool.thread_count();
}

auto Mandelbrot::set_kernel(const char* name) -> bool {
    const KernelSelection selection = find_kernel(name);
    if (selection.function == nullptr) {
        return false;
    }
    kernel_ = selection.function;
    kernel_name_ = selection.name;
    return true;
}
