#include <algorithm>
#include <cmath>
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
    image_width(_width),
    image_height(_height),
    #if defined (__AVX__)
        width((image_width + 7) & ~7),
    #elif defined(__ARM_NEON)
        width((image_width + 3) & ~3),
    #else
        width(image_width),
    #endif
    height(image_height),
    max_iterations(_maxIterations),
    r_data(static_cast<double*>(operator new(width * sizeof(double), std::align_val_t(32), std::nothrow))),
    i_data(static_cast<double*>(operator new(height * sizeof(double), std::align_val_t(32), std::nothrow))),
    iteration_data(static_cast<float*>(operator new(width * height * sizeof(float), std::align_val_t(32), std::nothrow))),
    thread_pool(static_cast<size_t>(std::max(1U, std::thread::hardware_concurrency()))),
    image(image_height, image_width, CV_8UC3) {
        if (r_data == nullptr || i_data == nullptr || iteration_data == nullptr || !image.isContinuous()) {
            std::cerr << "Memory allocation failed.\n";
            std::exit(MemoryAllocationError);
        }
}
Mandelbrot::~Mandelbrot() {
    operator delete(r_data, std::align_val_t(32), std::nothrow);
    operator delete(i_data, std::align_val_t(32), std::nothrow);
    operator delete(iteration_data, std::align_val_t(32), std::nothrow);
}
void Mandelbrot::setView(double new_zoom, double new_offset_x, double new_offset_y) {
    zoom = new_zoom;
    delta = 4.0 / std::max(image_width, image_height) / zoom;
    offsetX = new_offset_x;
    offsetY = new_offset_y;
    for (size_t x = 0; x < width; ++x) {
        r_data[x] = std::fma(x - (image_width / 2.0), delta, offsetX);
    }
    for (size_t y = 0; y < height; ++y) {
        i_data[y] = std::fma(y - (image_height / 2.0), delta, offsetY);
    }
}
void Mandelbrot::row_task(size_t y) {
    // iterate part
    float* const iteration_row = iteration_data + y * width;
    #if defined (__AVX__)
        const __m256d y_vec = _mm256_set1_pd(i_data[y]);
        const __m256d zeros = _mm256_setzero_pd();
        const __m256d ones = _mm256_set1_pd(1.0);
        const __m256d fours = _mm256_set1_pd(4.0);
        for (size_t x = 0; x < width; x += 4) {
            const __m256d x_vec = _mm256_load_pd(r_data + x);
            __m256d r = zeros;
            __m256d i = zeros;
            __m256d ri = zeros;
            __m256d r2 = zeros;
            __m256d i2 = zeros;
            __m256d iteration_vec = zeros;
            __m256d mod_squared_vec = zeros;
            for (uint32_t iter = 0; iter < max_iterations; ++iter) {
                __m256d continue_mask = _mm256_cmp_pd(mod_squared_vec, fours, _CMP_LT_OQ);
                if (_mm256_movemask_pd(continue_mask) == 0)
                    break;
                iteration_vec = _mm256_add_pd(iteration_vec, _mm256_and_pd(continue_mask, ones));
                i = _mm256_add_pd(_mm256_add_pd(ri, ri), y_vec);
                r = _mm256_add_pd(r2, _mm256_sub_pd(x_vec, i2));
                i2 = _mm256_mul_pd(i, i);
                r2 = _mm256_mul_pd(r, r);
                ri = _mm256_mul_pd(r, i);
                mod_squared_vec = _mm256_add_pd(r2, i2);
            }
            __m128 iteration_result = _mm256_cvtpd_ps(iteration_vec);
            _mm_store_ps(iteration_row + x, iteration_result);
        }
    #elif defined(__ARM_NEON)
        const float64x2_t y_vec = vdupq_n_f64(i_data[y]);
        const float64x2_t zeros = vdupq_n_f64(0.0);
        const float64x2_t ones = vdupq_n_f64(1.0);
        const float64x2_t fours = vdupq_n_f64(4.0);
        for (size_t x = 0; x < width; x += 2) {
            const float64x2_t x_vec = vld1q_f64(r_data + x);
            float64x2_t r = zeros;
            float64x2_t i = zeros;
            float64x2_t ri = zeros;
            float64x2_t r2 = zeros;
            float64x2_t i2 = zeros;
            float64x2_t iteration_vec = zeros;
            float64x2_t mod_squared_vec = zeros;
            for (uint32_t iter = 0; iter < max_iterations; ++iter) {
                uint64x2_t mask = vcltq_f64(mod_squared_vec, fours);
                if (vaddvq_u64(mask) == 0)
                    break;
                iteration_vec = vaddq_f64(iteration_vec, vbslq_f64(mask, ones, zeros));
                i = vaddq_f64(vaddq_f64(ri, ri), y_vec);
                r = vaddq_f64(r2, vsubq_f64(x_vec, i2));
                i2 = vmulq_f64(i, i);
                r2 = vmulq_f64(r, r);
                ri = vmulq_f64(r, i);
                mod_squared_vec = vaddq_f64(r2, i2);
            }
            float32x2_t iteration_float = vcvt_f32_f64(iteration_vec);
            vst1_f32(iteration_row + x, iteration_float);
        }
    #else
        for (size_t x = 0; x < width; ++x) {
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
            iteration_row[x] = iter;
        }
    #endif
    // color part
    const float a = 4.0f;
    const float min_kelvin = 100.0f;
    const float max_kelvin = 6000.0f;
    uint8_t* const row_ptr = image.data + y * image.step;
    for (size_t x = 0; x < image_width; ++x) {
        float iter = iteration_row[x];
        if (iter >= max_iterations) {
            row_ptr[x * 3 + 0] = 0;
            row_ptr[x * 3 + 1] = 0;
            row_ptr[x * 3 + 2] = 0;
        } else {
            float kelvin = std::min(min_kelvin + a * iter, max_kelvin);
            float lightness = std::min(a * kelvin / max_kelvin, 1.0f);
            float b = std::fma(0.06375f, kelvin, -127.5f);
            float g = std::fma(0.04636f, kelvin, -23.18f);
            auto clamp = [](float value) {
                return static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
            };
            row_ptr[x * 3 + 0] = clamp(b * lightness);
            row_ptr[x * 3 + 1] = clamp(g * lightness);
            row_ptr[x * 3 + 2] = clamp(255 * lightness);
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