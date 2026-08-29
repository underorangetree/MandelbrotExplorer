#if defined(__ARM_NEON)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <arm_neon.h>
#include "ExitStatus.h"
#include "Mandelbrot.h"

constexpr size_t simd_size = 2;

Mandelbrot::Mandelbrot(size_t _width, size_t _height, uint32_t _maxIterations):
    width((_width + 3) & ~3),
    height(_height),
    max_iterations(_maxIterations),
    r_data(static_cast<double*>(operator new(width * sizeof(double), std::align_val_t(32), std::nothrow))),
    i_data(static_cast<double*>(operator new(height * sizeof(double), std::align_val_t(32), std::nothrow))),
    thread_pool(static_cast<size_t>(std::max(1U, std::thread::hardware_concurrency()))),
    image(height, width, CV_8UC3) {
        if (r_data == nullptr || i_data == nullptr || !image.isContinuous()) {
            std::cerr << "Memory allocation failed.\n";
            std::exit(MemoryAllocationError);
        }
    color_map.reserve(max_iterations + 1);
    const float a = 4.0f;
    const float min_kelvin = 100.0f;
    const float max_kelvin = 6000.0f;
    for (uint iter = 0; iter < max_iterations; ++iter) {
        int64_t kelvin = std::min(min_kelvin + a * iter, max_kelvin);
        auto clamp = [](int64_t value) {
            return static_cast<uint8_t>(std::clamp(value, static_cast<int64_t>(0), static_cast<int64_t>(255)));
        };
        int64_t b = 51 * a * kelvin * (kelvin - 2000) / 800 / max_kelvin;
        int64_t g = 51 * a * kelvin * (kelvin - 500) / 1100 / max_kelvin;
        int64_t r = 255 * a * kelvin / max_kelvin;
        color_map.push_back({static_cast<uint8_t>(clamp(b)), static_cast<uint8_t>(clamp(g)), static_cast<uint8_t>(clamp(r))});
    }
    color_map.push_back({0, 0, 0});
}

Mandelbrot::~Mandelbrot() {
    operator delete(r_data, std::align_val_t(32), std::nothrow);
    operator delete(i_data, std::align_val_t(32), std::nothrow);
}

void Mandelbrot::setView(double new_zoom, double new_offset_x, double new_offset_y) {
    zoom = new_zoom;
    delta = 4.0 / std::max(width, height) / zoom;
    offset_x = new_offset_x;
    offset_y = new_offset_y;
    // TODO: 使用 NEON SIMD 优化 r_data 和 i_data 的计算
    for (size_t x = 0; x < width; ++x) {
        r_data[x] = std::fma(x - (width / 2.0), delta, offset_x);
    }
    for (size_t y = 0; y < height; ++y) {
        i_data[y] = std::fma(y - (height / 2.0), delta, offset_y);
    }
}

void Mandelbrot::row_task(size_t y) {
    // TODO: 交错
    const float a = 4.0f;
    const float min_kelvin = 100.0f;
    const float max_kelvin = 6000.0f;
    uint8_t* const row_ptr = image.data + y * image.step;
    const size_t simd_size = 2;
    alignas(16) int64_t iterations[simd_size];
    const float64x2_t y_vec = vdupq_n_f64(i_data[y]);
    const float64x2_t zeros = vdupq_n_f64(0.0);
    const uint64x2_t zeros_int = vdupq_n_u64(0);
    const float64x2_t twos = vdupq_n_f64(2.0);
    const float64x2_t fours = vdupq_n_f64(4.0);
    for (size_t x = 0; x < width; x += 2) {
        const float64x2_t x_vec = vld1q_f64(r_data + x);
        float64x2_t r = zeros;
        float64x2_t i = zeros;
        float64x2_t ri = zeros;
        float64x2_t r2 = zeros;
        float64x2_t i2 = zeros;
        uint64x2_t iteration_vec = zeros_int;
        float64x2_t mod_squared_vec = zeros;
        for (uint32_t iter = 0; iter < max_iterations; ++iter) {
            uint64x2_t continue_mask = vcltq_f64(mod_squared_vec, fours);
            if (vaddvq_u64(continue_mask) == 0)
                break;
            iteration_vec = vsubq_u64(iteration_vec, continue_mask);
            i = vfmaq_f64(y_vec, ri, twos);
            r = vaddq_f64(r2, vsubq_f64(x_vec, i2));
            i2 = vmulq_f64(i, i);
            r2 = vmulq_f64(r, r);
            ri = vmulq_f64(r, i);
            mod_squared_vec = vaddq_f64(r2, i2);
        }
        vst1q_s64(iterations, vreinterpretq_s64_u64(iteration_vec));
        for (size_t dx = 0; dx < simd_size; ++dx) {
            uint8_t* const pixel_ptr = row_ptr + (x + dx) * 3;
            int64_t iter = iterations[dx];
            pixel_ptr[0] = color_map[iter][0];
            pixel_ptr[1] = color_map[iter][1];
            pixel_ptr[2] = color_map[iter][2];
        }
    }
}

cv::Mat& Mandelbrot::generate() {
    for (size_t y = 0; y < height; ++y) {
        thread_pool.enqueue([this, y]() { row_task(y); });
    }
    thread_pool.wait_all_idle();
    return image;
}

#endif