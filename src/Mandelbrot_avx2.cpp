#if defined(__AVX2__)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <immintrin.h>
#include "ExitStatus.h"
#include "Mandelbrot.h"

constexpr size_t simd_size = 4;

Mandelbrot::Mandelbrot(size_t _width, size_t _height, size_t _maxIterations):
    width((_width + 7) & ~7), // Align to 2*simd_size
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
    setView(zoom, offset_x, offset_y);
    if (max_iterations == 0 || max_iterations > 1000000) {
        std::cerr << "max_iterations must be greater than 0 and less than or equal to 1000000.\n";
        std::exit(InvalidArgument);
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
    const __m256d delta_vec = _mm256_set1_pd(delta);
    const __m256d offset_x_vec = _mm256_set1_pd(offset_x);
    const __m256d offset_y_vec = _mm256_set1_pd(offset_y);
    const __m256d half_width_vec = _mm256_set1_pd(width / 2.0);
    const __m256d half_height_vec = _mm256_set1_pd(height / 2.0);
    for (size_t x = 0; x < width; x += simd_size) {
        __m256d x_vec = _mm256_set_pd(x + 3, x + 2, x + 1, x);
        __m256d offset = _mm256_sub_pd(x_vec, half_width_vec);
        __m256d r_vec = _mm256_fmadd_pd(offset, delta_vec, offset_x_vec);
        _mm256_store_pd(r_data + x, r_vec);
    }
    size_t y = 0;
    for (; y <= height - simd_size; y += simd_size) {
        __m256d y_vec = _mm256_set_pd(y + 3, y + 2, y + 1, y);
        __m256d offset = _mm256_sub_pd(y_vec, half_height_vec);
        __m256d i_vec = _mm256_fmadd_pd(offset, delta_vec, offset_y_vec);
        _mm256_store_pd(i_data + y, i_vec);
    }
    for (; y < height; ++y) {
        i_data[y] = std::fma((y - height / 2.0) * delta, 1.0, offset_y);
    }
}

void Mandelbrot::row_task(size_t y) {
    uint8_t* const row_ptr = image.data + y * image.step;
    alignas(32) int64_t iterations_a[simd_size];
    alignas(32) int64_t iterations_b[simd_size];
    const __m256d y_vec = _mm256_set1_pd(i_data[y]);
    const __m256d zeros = _mm256_setzero_pd();
    const __m256d sixteenth = _mm256_set1_pd(1.0 / 16.0);
    const __m256d quarter = _mm256_set1_pd(0.25);
    const __m256d ones = _mm256_set1_pd(1.0);
    const __m256d twos = _mm256_set1_pd(2.0);
    const __m256d fours = _mm256_set1_pd(4.0);
    const __m256d y2 = _mm256_mul_pd(y_vec, y_vec);
    const __m256d y2_over_4 = _mm256_mul_pd(y2, quarter);
    const __m256i max_iter_vec = _mm256_set1_epi64x(static_cast<int64_t>(max_iterations));
    for (size_t x = 0; x < width; x += 2 * simd_size) {
        const __m256d x_vec_a = _mm256_load_pd(r_data + x);
        __m256d r_a = zeros;
        __m256d i_a = zeros;
        __m256d ri_a = zeros;
        __m256d r2_a = zeros;
        __m256d i2_a = zeros;
        __m256i iteration_vec_a;
        __m256d mod_squared_vec_a = zeros;
        const __m256d x_vec_b = _mm256_load_pd(r_data + x + simd_size);
        __m256d r_b = zeros;
        __m256d i_b = zeros;
        __m256d ri_b = zeros;
        __m256d r2_b = zeros;
        __m256d i2_b = zeros;
        __m256i iteration_vec_b;
        __m256d mod_squared_vec_b = zeros;
        // 进循环前检测点是否位于主心形线或周期2圆盘内，不逃逸的点跳过迭代
        // 主心形线: q = (x - 0.25)^2 + y^2, 满足 q * (q + (x - 0.25)) <= 0.25 * y^2 的点在内部
        // 周期2圆盘: 满足 (x + 1)^2 + y^2 <= 1/16 的点在内部
        // vec a
        __m256d xq = _mm256_sub_pd(x_vec_a, quarter);
        __m256d q = _mm256_fmadd_pd(xq, xq, y2);
        __m256d in_cardioid_mask = _mm256_cmp_pd(_mm256_mul_pd(q, _mm256_add_pd(q, xq)), y2_over_4, _CMP_LE_OQ);
        __m256d xp1 = _mm256_add_pd(x_vec_a, ones);
        __m256d in_bulb_mask = _mm256_cmp_pd(_mm256_fmadd_pd(xp1, xp1, y2), sixteenth, _CMP_LE_OQ);
        __m256d continue_mask_a = _mm256_cmp_pd(_mm256_or_pd(in_cardioid_mask, in_bulb_mask), zeros, _CMP_EQ_OQ);
        // vec b
        xq = _mm256_sub_pd(x_vec_b, quarter);
        q = _mm256_fmadd_pd(xq, xq, y2);
        in_cardioid_mask = _mm256_cmp_pd(_mm256_mul_pd(q, _mm256_add_pd(q, xq)), y2_over_4, _CMP_LE_OQ);
        xp1 = _mm256_add_pd(x_vec_b, ones);
        in_bulb_mask = _mm256_cmp_pd(_mm256_fmadd_pd(xp1, xp1, y2), sixteenth, _CMP_LE_OQ);
        __m256d continue_mask_b = _mm256_cmp_pd(_mm256_or_pd(in_cardioid_mask, in_bulb_mask), zeros, _CMP_EQ_OQ);
        iteration_vec_a = _mm256_andnot_si256(_mm256_castpd_si256(continue_mask_a), max_iter_vec);
        iteration_vec_b = _mm256_andnot_si256(_mm256_castpd_si256(continue_mask_b), max_iter_vec);
        for (uint32_t iter = 0; iter < max_iterations; ++iter) {
            continue_mask_a = _mm256_and_pd(continue_mask_a, _mm256_cmp_pd(mod_squared_vec_a, fours, _CMP_LT_OQ));
            continue_mask_b = _mm256_and_pd(continue_mask_b, _mm256_cmp_pd(mod_squared_vec_b, fours, _CMP_LT_OQ));
            if (_mm256_movemask_pd(continue_mask_a) == 0 && _mm256_movemask_pd(continue_mask_b) == 0)
                break;
            // vec a
            iteration_vec_a = _mm256_sub_epi64(iteration_vec_a, _mm256_castpd_si256(continue_mask_a));
            i_a = _mm256_fmadd_pd(twos, ri_a, y_vec);
            r_a = _mm256_add_pd(r2_a, _mm256_sub_pd(x_vec_a, i2_a));
            i2_a = _mm256_mul_pd(i_a, i_a);
            r2_a = _mm256_mul_pd(r_a, r_a);
            ri_a = _mm256_mul_pd(r_a, i_a);
            mod_squared_vec_a = _mm256_add_pd(r2_a, i2_a);
            // vec b
            iteration_vec_b = _mm256_sub_epi64(iteration_vec_b, _mm256_castpd_si256(continue_mask_b));
            i_b = _mm256_fmadd_pd(twos, ri_b, y_vec);
            r_b = _mm256_add_pd(r2_b, _mm256_sub_pd(x_vec_b, i2_b));
            i2_b = _mm256_mul_pd(i_b, i_b);
            r2_b = _mm256_mul_pd(r_b, r_b);
            ri_b = _mm256_mul_pd(r_b, i_b);
            mod_squared_vec_b = _mm256_add_pd(r2_b, i2_b);
        }
        _mm256_store_si256(reinterpret_cast<__m256i*>(iterations_a), iteration_vec_a);
        _mm256_store_si256(reinterpret_cast<__m256i*>(iterations_b), iteration_vec_b);
        for (size_t dx = 0; dx < simd_size; ++dx) {
            uint8_t* const pixel_ptr = row_ptr + (x + dx) * 3;
            uint64_t iter = iterations_a[dx];
            pixel_ptr[0] = color_map[iter][0];
            pixel_ptr[1] = color_map[iter][1];
            pixel_ptr[2] = color_map[iter][2];
        }
        for (size_t dx = 0; dx < simd_size; ++dx) {
            uint8_t* const pixel_ptr = row_ptr + (x + simd_size + dx) * 3;
            uint64_t iter = iterations_b[dx];
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