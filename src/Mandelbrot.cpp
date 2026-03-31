#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#if defined(__AVX__)
    #include <immintrin.h>
#elif defined(__ARM_NEON)
    #include <arm_neon.h>
#endif
#include "ExitStatus.h"
#include "Mandelbrot.h"
Mandelbrot::Mandelbrot(size_t _width, size_t _height, uint32_t _maxIterations):
    #if defined (__AVX__)
        width((_width + 3) & ~3),
    #elif defined(__ARM_NEON)
        width((_width + 1) & ~1),
    #else
        width(_width),
    #endif
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
}
Mandelbrot::~Mandelbrot() {
    operator delete(r_data, std::align_val_t(32), std::nothrow);
    operator delete(i_data, std::align_val_t(32), std::nothrow);
}
void Mandelbrot::setView(double new_zoom, double new_offset_x, double new_offset_y) {
    zoom = new_zoom;
    delta = 4.0 / std::max(width, height) / zoom;
    offsetX = new_offset_x;
    offsetY = new_offset_y;
    for (size_t x = 0; x < width; ++x) {
        r_data[x] = std::fma(x - (width / 2.0), delta, offsetX);
    }
    for (size_t y = 0; y < height; ++y) {
        i_data[y] = std::fma(y - (height / 2.0), delta, offsetY);
    }
}
void Mandelbrot::row_task(size_t y) {
    const float a = 4.0f;
    const float min_kelvin = 100.0f;
    const float max_kelvin = 6000.0f;
    uint8_t* const row_ptr = image.data + y * image.step;
    #if defined(__AVX__)
        const size_t simd_size = 4;
        alignas(32) int64_t iterations[simd_size];
        const __m256d y_vec = _mm256_set1_pd(i_data[y]);
        const __m256d zeros = _mm256_setzero_pd();
        const __m256i zeros_int = _mm256_setzero_si256();
        const __m256d twos = _mm256_set1_pd(2.0);
        const __m256d fours = _mm256_set1_pd(4.0);
        for (size_t x = 0; x < width; x += simd_size) {
            const __m256d x_vec = _mm256_load_pd(r_data + x);
            __m256d r = zeros;
            __m256d i = zeros;
            __m256d ri = zeros;
            __m256d r2 = zeros;
            __m256d i2 = zeros;
            __m256i iteration_vec = zeros_int;
            __m256d mod_squared_vec = zeros;
            for (uint32_t iter = 0; iter < max_iterations; ++iter) {
                __m256d continue_mask = _mm256_cmp_pd(mod_squared_vec, fours, _CMP_LT_OQ);
                if (_mm256_movemask_pd(continue_mask) == 0)
                    break;
                iteration_vec = _mm256_sub_epi64(iteration_vec, _mm256_castpd_si256(continue_mask));
                #if defined(__FMA__)
                    i = _mm256_fmadd_pd(twos, ri, y_vec);
                #else
                    i = _mm256_add_pd(_mm256_add_pd(ri, ri), y_vec);
                #endif
                i = _mm256_fmadd_pd(twos, ri, y_vec);
                r = _mm256_add_pd(r2, _mm256_sub_pd(x_vec, i2));
                i2 = _mm256_mul_pd(i, i);
                r2 = _mm256_mul_pd(r, r);
                ri = _mm256_mul_pd(r, i);
                mod_squared_vec = _mm256_add_pd(r2, i2);
            }
            _mm256_store_si256(reinterpret_cast<__m256i*>(iterations), iteration_vec);
            for (size_t dx = 0; dx < simd_size; ++dx) {
                uint8_t* const pixel_ptr = row_ptr + (x + dx) * 3;
                int64_t iter = iterations[dx];
                if (iter >= max_iterations) {
                    pixel_ptr[0] = 0;
                    pixel_ptr[1] = 0;
                    pixel_ptr[2] = 0;
                } else {
                    int64_t kelvin = std::min(min_kelvin + a * iter, max_kelvin);
                    auto clamp = [](int64_t value) {
                        return static_cast<uint8_t>(std::clamp(value, static_cast<int64_t>(0), static_cast<int64_t>(255)));
                    };
                    int64_t b = 51 * a * kelvin * (kelvin - 2000) / 800 / max_kelvin;
                    int64_t g = 51 * a * kelvin * (kelvin - 500) / 1100 / max_kelvin;
                    int64_t r = 255 * a * kelvin / max_kelvin;
                    pixel_ptr[0] = clamp(b);
                    pixel_ptr[1] = clamp(g);
                    pixel_ptr[2] = clamp(r);
                }
            }
        }
    #elif defined(__ARM_NEON)
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
                if (iter >= max_iterations) {
                    pixel_ptr[0] = 0;
                    pixel_ptr[1] = 0;
                    pixel_ptr[2] = 0;
                } else {
                    int64_t kelvin = std::min(min_kelvin + a * iter, max_kelvin);
                    auto clamp = [](int64_t value) {
                        return static_cast<uint8_t>(std::clamp(value, static_cast<int64_t>(0), static_cast<int64_t>(255)));
                    };
                    int64_t b = 51 * a * kelvin * (kelvin - 2000) / 800 / max_kelvin;
                    int64_t g = 51 * a * kelvin * (kelvin - 500) / 1100 / max_kelvin;
                    int64_t r = 255 * a * kelvin / max_kelvin;
                    pixel_ptr[0] = clamp(b);
                    pixel_ptr[1] = clamp(g);
                    pixel_ptr[2] = clamp(r);
                }
            }
        }
    #else
        for (size_t x = 0; x < width; ++x) {
            uint8_t* const pixel_ptr = row_ptr + x * 3;
            double r = 0.0, i = 0.0;
            double ri = 0.0;
            double r2 = 0.0, i2 = 0.0;
            uint32_t iter = 0;
            while (r2 + i2 <= 4.0 && iter < max_iterations) {
                i = std::fma(2.0, ri, i_data[y]);
                r = r2 - i2 + r_data[x];
                i2 = i * i;
                r2 = r * r;
                ri = r * i;
                ++iter;
            }
            if (iter >= max_iterations) {
                pixel_ptr[0] = 0;
                pixel_ptr[1] = 0;
                pixel_ptr[2] = 0;
            } else {
                int64_t kelvin = std::min(min_kelvin + a * iter, max_kelvin);
                auto clamp = [](int64_t value) {
                    return static_cast<uint8_t>(std::clamp(value, static_cast<int64_t>(0), static_cast<int64_t>(255)));
                };
                int64_t b = 51 * a * kelvin * (kelvin - 2000) / 800 / max_kelvin;
                int64_t g = 51 * a * kelvin * (kelvin - 500) / 1100 / max_kelvin;
                int64_t r = 255 * a * kelvin / max_kelvin;
                pixel_ptr[0] = clamp(b);
                pixel_ptr[1] = clamp(g);
                pixel_ptr[2] = clamp(r);
            }
        }
    #endif
}
cv::Mat& Mandelbrot::generate() {
    for (size_t y = 0; y < height; ++y) {
        thread_pool.enqueue([this, y]() { row_task(y); });
    }
    thread_pool.wait_all_idle();
    return image;
}