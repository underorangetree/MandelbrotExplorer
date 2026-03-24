#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <new>
#if defined(__AVX__)
    #include <immintrin.h>
#elif defined(__ARM_NEON)
    #include <arm_neon.h>
#endif
#include <chrono> // debug
#include "Mandelbrot.h"
Mandelbrot::Mandelbrot(int _width, int _height, int _maxIterations) noexcept :
    #if defined (__AVX__)
        width((_width + 7) & ~7),
    #elif defined(__ARM_NEON)
        width((_width + 3) & ~3),
    #else
        width(_width),
    #endif
    height(_height),
    max_iterations(_maxIterations),
    r_data(static_cast<double*>(operator new(width * sizeof(double), std::nothrow))),
    i_data(static_cast<double*>(operator new(height * sizeof(double), std::nothrow))),
    iteration_data(static_cast<float*>(operator new(width * height * sizeof(float), std::align_val_t(32), std::nothrow))),
    image(_height, _width, CV_8UC3),
    num_threads(std::max(1U, std::thread::hardware_concurrency())) {
        if (r_data == nullptr || i_data == nullptr || iteration_data == nullptr || !image.isContinuous())
            std::abort(); // 抛什么异常，直接爆了
        elapsed_times.resize(3); // debug
}
Mandelbrot::~Mandelbrot() {
    operator delete(r_data, std::nothrow);
    operator delete(i_data, std::nothrow);
    operator delete(iteration_data, std::align_val_t(32), std::nothrow);
}
void Mandelbrot::setView(double new_zoom, double new_offset_x, double new_offset_y) {
    zoom = new_zoom;
    delta = 4.0 / std::max(width, height) / zoom;
    offsetX = new_offset_x;
    offsetY = new_offset_y;
    // This is not a performance hotspot, SIMD is not used.
    for (int x = 0; x < width; ++x) {
        r_data[x] = std::fma(static_cast<double>(x - (width >> 1)), delta, offsetX);
    }
    for (int y = 0; y < height; ++y) {
        i_data[y] = std::fma(static_cast<double>(y - (height >> 1)), delta, offsetY);
    }
}
void Mandelbrot::rows_generate(int startY, int endY) {
    for (int y = startY; y < endY; ++y) {
        float* iteration_row = iteration_data + y * width;
        #if defined (__AVX__)
            const __m256d y_vec = _mm256_set1_pd(i_data[y]);
            const __m256d zeros = _mm256_setzero_pd();
            for (int x = 0; x < width; x+=4) {
                const __m256d x_vec = _mm256_load_pd(r_data + x);
                __m256d r = zeros;
                __m256d i = zeros;
                __m256d r2 = zeros;
                __m256d i2 = zeros;
                __m256d iteration = zeros;
                __m256d mod_squared = zeros;
                __m256d escape_radius_squared = zeros;
                for (int iter = 0; iter < max_iterations; ++iter) {
                    __m256d mask = _mm256_cmp_pd(mod_squared, _mm256_set1_pd(4.0), _CMP_LT_OQ);
                    if (_mm256_movemask_pd(mask) == 0)
                        break;
                    iteration = _mm256_add_pd(iteration, _mm256_and_pd(_mm256_set1_pd(1.0), mask));
                    __m256d temp_r = _mm256_add_pd(_mm256_sub_pd(r2, i2), x_vec);
                    #if defined(__FMA__)
                    i = _mm256_fmadd_pd(_mm256_mul_pd(_mm256_set1_pd(2.0), r), i, y_vec);
                    #else
                    i = _mm256_add_pd(_mm256_mul_pd(_mm256_mul_pd(_mm256_set1_pd(2.0), r), i), y_vec);
                    #endif
                    r = temp_r;
                    r2 = _mm256_mul_pd(r, r);
                    i2 = _mm256_mul_pd(i, i);
                    mod_squared = _mm256_add_pd(r2, i2);
                    escape_radius_squared = _mm256_and_pd(mod_squared, mask);
                }
                // 考虑辅助函数f(x) = f(x^2)-1，f(4) = 0，f(16) = 1，可以得到f(x) = log(log(x)/log(4))/log(2)，不妨用g(x) = (8-x)/12近似
                __m256d extra_iteration = _mm256_mul_pd(_mm256_sub_pd(_mm256_set1_pd(8.0), escape_radius_squared), _mm256_set1_pd(1.0/12.0));
                iteration = _mm256_add_pd(iteration, extra_iteration);
                __m128 iteration_float = _mm256_cvtpd_ps(iteration);
                _mm_store_ps(iteration_row + x, iteration_float);
            }
        #elif defined(__ARM_NEON)
            const float64x2_t y_vec = vdupq_n_f64(i_data[y]);
            const float64x2_t zeros = vdupq_n_f64(0.0);
            const float64x2_t ones = vdupq_n_f64(1.0);
            for (int x = 0; x < width; x+=2) {
                const float64x2_t x_vec = vld1q_f64(r_data + x);
                float64x2_t r = zeros;
                float64x2_t i = zeros;
                float64x2_t r2 = zeros;
                float64x2_t i2 = zeros;
                float64x2_t iteration = zeros;
                float64x2_t mod_squared = zeros;
                float64x2_t escape_radius_squared = zeros;
                for (int iter = 0; iter < max_iterations; ++iter) {
                    uint64x2_t mask = vcltq_f64(mod_squared, vdupq_n_f64(4.0));
                    if (vaddvq_u64(mask) == 0)
                        break;
                    iteration = vaddq_f64(iteration, vbslq_f64(mask, ones, zeros));
                    float64x2_t temp_r = vaddq_f64(vsubq_f64(r2, i2), x_vec);
                    i = vaddq_f64(vmulq_f64(vmulq_f64(vdupq_n_f64(2.0), r), i), y_vec);
                    r = temp_r;
                    r2 = vmulq_f64(r, r);
                    i2 = vmulq_f64(i, i);
                    mod_squared = vaddq_f64(r2, i2);
                    escape_radius_squared = vbslq_f64(mask, mod_squared, zeros);
                }
                float64x2_t extra_iteration = vmulq_f64(vsubq_f64(vdupq_n_f64(8.0), escape_radius_squared), vdupq_n_f64(1.0/12.0));
                iteration = vaddq_f64(iteration, extra_iteration);
                float32x2_t iteration_float = vcvt_f32_f64(iteration);
                vst1_f32(iteration_row + x, iteration_float);
            }
        #else
            for (int x = 0; x < width; ++x) {
                double r = 0.0, i = 0.0;
                double r2 = 0.0, i2 = 0.0;
                int iter = 0;
                while (r2 + i2 <= 4.0 && iter < max_iterations) {
                    i = std::fma(2.0 * r, i, i_data[y]);
                    r = std::fma(r, r, -i2 + r_data[x]);
                    r2 = r * r;
                    i2 = i * i;
                    ++iter;
                }
                float extra_iteration = static_cast<float>((8.0 - (r2 + i2)) / 12.0);
                iteration_row[x] = iter + extra_iteration;
            }
        #endif
    }
}
void Mandelbrot::rows_color_render(int startY, int endY) {
    const float minKelvin = 100.0;
    const float maxKelvin = 6000.0;
    for (int y = startY; y < endY; ++y) {
        uint8_t* rowPtr = image.data + y * image.step;
        float* iterationRow = iteration_data + y * width;
        for (int x = 0; x < image.cols; ++x) {
            float iter = iterationRow[x];
            if (iter >= max_iterations) {
                rowPtr[x * 3 + 0] = 0;
                rowPtr[x * 3 + 1] = 0;
                rowPtr[x * 3 + 2] = 0;
            } else {
                float kelvin = std::min(minKelvin + 6 * iter, maxKelvin);
                float lightness = std::min(8 * kelvin / maxKelvin, 1.0f);
                float g = 99.47 * std::log(kelvin) - 619.2;
                float b = kelvin <= 1900 ? 0 : 138.5 * std::log(kelvin - 1000) - 942.9;
                auto clamp = [](float value) {
                    return static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
                };
                rowPtr[x * 3 + 0] = clamp(b * lightness);
                rowPtr[x * 3 + 1] = clamp(g * lightness);
                rowPtr[x * 3 + 2] = clamp(255 * lightness);
            }
        }
    }
}
void Mandelbrot::generate() {
    auto start_time = std::chrono::high_resolution_clock::now(); // debug
    threads.clear();
    for (unsigned int threadId = 0; threadId < num_threads; ++threadId) {
        const int startY = threadId * height / num_threads;
        const int endY = (threadId + 1) * height / num_threads;
        threads.emplace_back(&Mandelbrot::rows_generate, this, startY, endY);
    }
    for (auto& thread : threads) {
        thread.join();
    }
    elapsed_times[0].push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time).count()); // debug
}
cv::Mat& Mandelbrot::render() {
    auto start_time = std::chrono::high_resolution_clock::now(); // debug
    threads.clear();
    for (unsigned int threadId = 0; threadId < num_threads; ++threadId) {
        const int startY = threadId * height / num_threads;
        const int endY = (threadId + 1) * height / num_threads;
        threads.emplace_back(&Mandelbrot::rows_color_render, this, startY, endY);
    }
    for (auto& thread : threads) {
        thread.join();
    }
    elapsed_times[1].push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time).count()); // debug
    start_time = std::chrono::high_resolution_clock::now(); // debug
    // 泛光
    const float threshold = 200.0f;
    const int blurSize = 15;
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::Mat brightMask;
    cv::threshold(gray, brightMask, threshold, 255, cv::THRESH_BINARY);
    cv::Mat brightPart;
    image.copyTo(brightPart, brightMask);
    cv::Mat blurred;
    cv::GaussianBlur(brightPart, blurred, cv::Size(blurSize, blurSize), 0);
    cv::addWeighted(image, 1.0f, blurred, 0.2f, 0, image);
    elapsed_times[2].push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time).count()); // debug
    return image;
}