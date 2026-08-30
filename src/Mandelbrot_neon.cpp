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

Mandelbrot::Mandelbrot(size_t _width, size_t _height, size_t _maxIterations):
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
    if (max_iterations == 0 || max_iterations > 1000000) {
        std::cerr << "max_iterations must be greater than 0 and less than or equal to 1000000.\n";
        std::exit(InvalidArgument);
    }
    const float a = 4.0f;
    const float min_kelvin = 100.0f;
    const float max_kelvin = 6000.0f;
    for (size_t iter = 0; iter < max_iterations; ++iter) {
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
    const float64x2_t delta_vec = vdupq_n_f64(delta);
    const float64x2_t offset_x_vec = vdupq_n_f64(offset_x);
    const float64x2_t offset_y_vec = vdupq_n_f64(offset_y);
    const float64x2_t half_width_vec = vdupq_n_f64(width / 2.0);
    const float64x2_t half_height_vec = vdupq_n_f64(height / 2.0);
    for (size_t x = 0; x < width; x += simd_size) {
        double xs[2] = {static_cast<double>(x), static_cast<double>(x + 1)};
        float64x2_t x_vec = vld1q_f64(xs);
        float64x2_t offset = vsubq_f64(x_vec, half_width_vec);
        float64x2_t r_vec = vmlaq_f64(offset_x_vec, offset, delta_vec);
        vst1q_f64(r_data + x, r_vec);
    }
    size_t y = 0;
    for (; y <= height - simd_size; y += simd_size) {
        double ys[2] = {static_cast<double>(y), static_cast<double>(y + 1)};
        float64x2_t y_vec = vld1q_f64(ys);
        float64x2_t offset = vsubq_f64(y_vec, half_height_vec);
        float64x2_t i_vec = vmlaq_f64(offset_y_vec, offset, delta_vec);
        vst1q_f64(i_data + y, i_vec);
    }
    for (; y < height; ++y) {
        i_data[y] = std::fma((y - height / 2.0) * delta, 1.0, offset_y);
    }
}

void Mandelbrot::row_task(size_t y) {
    // TODO: 交错
    uint8_t* const row_ptr = image.data + y * image.step;
    alignas(16) int64_t iterations_a[simd_size];
    alignas(16) int64_t iterations_b[simd_size];
    const float64x2_t y_vec = vdupq_n_f64(i_data[y]);
    const uint64x2_t zeros_int = vdupq_n_u64(0);
    const float64x2_t zeros = vdupq_n_f64(0.0);
    const float64x2_t sixteenth = vdupq_n_f64(1.0 / 16.0);
    const float64x2_t quarter = vdupq_n_f64(0.25);
    const float64x2_t ones = vdupq_n_f64(1.0);
    const float64x2_t twos = vdupq_n_f64(2.0);
    const float64x2_t fours = vdupq_n_f64(4.0);
    const float64x2_t y2 = vmulq_f64(y_vec, y_vec);
    const float64x2_t y2_over_4 = vmulq_f64(y2, quarter);
    const uint64x2_t max_iter_vec = vdupq_n_u64(static_cast<uint64_t>(max_iterations));
    for (size_t x = 0; x < width; x += simd_size) {
        const float64x2_t x_vec_a = vld1q_f64(r_data + x);
        float64x2_t r_a = zeros;
        float64x2_t i_a = zeros;
        float64x2_t ri_a = zeros;
        float64x2_t r2_a = zeros;
        float64x2_t i2_a = zeros;
        uint64x2_t iteration_vec_a = zeros_int;
        float64x2_t mod_squared_vec_a = zeros;
        const float64x2_t x_vec_b = vld1q_f64(r_data + x + simd_size);
        float64x2_t r_b = zeros;
        float64x2_t i_b = zeros;
        float64x2_t ri_b = zeros;
        float64x2_t r2_b = zeros;
        float64x2_t i2_b = zeros;
        uint64x2_t iteration_vec_b = zeros_int;
        float64x2_t mod_squared_vec_b = zeros;
        float64x2_t xq = vsubq_f64(x_vec_a, quarter);
        float64x2_t q = vmlaq_f64(y2, xq, xq);
        uint64x2_t in_cardioid_mask = vcleq_f64(vmulq_f64(q, vaddq_f64(q, xq)), y2_over_4);
        float64x2_t xp1 = vaddq_f64(x_vec_a, ones);
        uint64x2_t in_bulb_mask = vcleq_f64(vmlaq_f64(y2, xp1, xp1), sixteenth);
        uint64x2_t continue_mask_a = vceqq_u64(vorrq_u64(in_cardioid_mask, in_bulb_mask), vdupq_n_u64(0));
        xq = vsubq_f64(x_vec_b, quarter);
        q = vmlaq_f64(y2, xq, xq);
        in_cardioid_mask = vcleq_f64(vmulq_f64(q, vaddq_f64(q, xq)), y2_over_4);
        xp1 = vaddq_f64(x_vec_b, ones);
        in_bulb_mask = vcleq_f64(vmlaq_f64(y2, xp1, xp1), sixteenth);
        uint64x2_t continue_mask_b = vceqq_u64(vorrq_u64(in_cardioid_mask, in_bulb_mask), vdupq_n_u64(0));
        iteration_vec_a = vbicq_u64(max_iter_vec, continue_mask_a);
        iteration_vec_b = vbicq_u64(max_iter_vec, continue_mask_b);
        for (size_t iter = 0; iter < max_iterations; ++iter) {
            continue_mask_a = vandq_u64(continue_mask_a, vcltq_f64(mod_squared_vec_a, fours));
            continue_mask_b = vandq_u64(continue_mask_b, vcltq_f64(mod_squared_vec_b, fours));
            if (vaddvq_u64(continue_mask_a) == 0 && vaddvq_u64(continue_mask_b) == 0)
                break;
            // vec a
            iteration_vec_a = vsubq_u64(iteration_vec_a, continue_mask_a);
            i_a = vfmaq_f64(y_vec, ri_a, twos);
            r_a = vaddq_f64(r2_a, vsubq_f64(x_vec_a, i2_a));
            i2_a = vmulq_f64(i_a, i_a);
            r2_a = vmulq_f64(r_a, r_a);
            ri_a = vmulq_f64(r_a, i_a);
            mod_squared_vec_a = vaddq_f64(r2_a, i2_a);
            // vec b
            iteration_vec_b = vsubq_u64(iteration_vec_b, continue_mask_b);
            i_b = vfmaq_f64(y_vec, ri_b, twos);
            r_b = vaddq_f64(r2_b, vsubq_f64(x_vec_b, i2_b));
            i2_b = vmulq_f64(i_b, i_b);
            r2_b = vmulq_f64(r_b, r_b);
            ri_b = vmulq_f64(r_b, i_b);
            mod_squared_vec_b = vaddq_f64(r2_b, i2_b);
        }
        vst1q_s64(iterations_a, vreinterpretq_s64_u64(iteration_vec_a));
        vst1q_s64(iterations_b, vreinterpretq_s64_u64(iteration_vec_b));
        for (size_t dx = 0; dx < simd_size; ++dx) {
            uint8_t* const pixel_ptr = row_ptr + (x + dx) * 3;
            int64_t iter = iterations_a[dx];
            pixel_ptr[0] = color_map[iter][0];
            pixel_ptr[1] = color_map[iter][1];
            pixel_ptr[2] = color_map[iter][2];
        }
        for (size_t dx = 0; dx < simd_size; ++dx) {
            uint8_t* const pixel_ptr = row_ptr + (x + dx) * 3;
            int64_t iter = iterations_b[dx];
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