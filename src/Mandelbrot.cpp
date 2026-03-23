#include <algorithm>
#include <cmath>
#include <new>
#include <immintrin.h>
#include <chrono> // debug
#include "Mandelbrot.h"
Mandelbrot::Mandelbrot(int width, int height, int maxIterations) noexcept :
    width((width + 7) & ~7),
    height(height),
    maxIterations(maxIterations),
    rData(static_cast<double*>(operator new(width * height * sizeof(double), std::align_val_t(32), std::nothrow))),
    iData(static_cast<double*>(operator new(width * height * sizeof(double), std::align_val_t(32), std::nothrow))),
    iterationData(static_cast<double*>(operator new(width * height * sizeof(double), std::align_val_t(32), std::nothrow))),
    image(height, width, CV_8UC3),
    numThreads(std::thread::hardware_concurrency()) {
        if (rData == nullptr || iData == nullptr || iterationData == nullptr || !image.isContinuous())
            std::abort(); // 抛什么异常，直接爆了
        elapsedTimes.resize(3); // debug
}
Mandelbrot::~Mandelbrot() {
    operator delete(rData, std::align_val_t(32));
    operator delete(iData, std::align_val_t(32));
    operator delete(iterationData, std::align_val_t(32));
}
void Mandelbrot::setView(double newZoom, double newOffsetX, double newOffsetY) {
    zoom = newZoom;
    offsetX = newOffsetX;
    offsetY = newOffsetY;
}
void Mandelbrot::rowsGenerater(int startY, int endY) {
    for (int y = startY; y < endY; ++y) {
        double* iterationRow = iterationData + y * width;
        const __m256d y_vec = _mm256_set1_pd(std::fma(static_cast<double>(y - (height >> 1)), delta, offsetY));
        const __m256d zeros = _mm256_setzero_pd();
        for (int x = 0; x < width; x+=4) {
            const double baseX = std::fma(static_cast<double>(x - (width >> 1)), delta, offsetX);
            const __m256d x_vec = _mm256_set_pd(
                std::fma(3, delta, baseX),
                std::fma(2, delta, baseX),
                baseX + delta,
                baseX
            ); // 注意是反序
            __m256d r = zeros;
            __m256d i = zeros;
            __m256d r2 = zeros;
            __m256d i2 = zeros;
            __m256d iteration = zeros;
            __m256d modSquared = zeros;
            __m256d escapeRadiusSquared = zeros;
            for (int iter = 0; iter < maxIterations; ++iter) {
                __m256d mask = _mm256_cmp_pd(modSquared, _mm256_set1_pd(4.0), _CMP_LT_OQ);
                if (_mm256_movemask_pd(mask) == 0)
                    break;
                iteration = _mm256_add_pd(iteration, _mm256_and_pd(_mm256_set1_pd(1.0), mask));
                __m256d tempR = _mm256_add_pd(_mm256_sub_pd(r2, i2), x_vec);
                i = _mm256_fmadd_pd(_mm256_mul_pd(_mm256_set1_pd(2.0), r), i, y_vec);
                r = tempR;
                r2 = _mm256_mul_pd(r, r);
                i2 = _mm256_mul_pd(i, i);
                modSquared = _mm256_add_pd(r2, i2);
                escapeRadiusSquared = _mm256_and_pd(modSquared, mask);
            }
            // 考虑辅助函数f(x) = f(x^2)-1，f(4) = 0，f(16) = 1，可以得到f(x) = log(log(x)/log(4))/log(2)，不妨用g(x) = (8-x)/12近似
            __m256d temp = _mm256_div_pd(_mm256_sub_pd(_mm256_set1_pd(8.0), escapeRadiusSquared), _mm256_set1_pd(12.0));
            iteration = _mm256_add_pd(iteration, temp);
            _mm256_store_pd(iterationRow + x, iteration);
        }
    }
}
void Mandelbrot::rowsColorRender(int startY, int endY) {
    const float minKelvin = 0.0;
    const float maxKelvin = 6000.0;
    for (int y = startY; y < endY; ++y) {
        uint8_t* rowPtr = image.data + y * image.step;
        double* iterationRow = iterationData + y * width;
        for (int x = 0; x < width; ++x) {
            float iter = static_cast<float>(iterationRow[x]);
            if (iter >= maxIterations) {
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
double* Mandelbrot::generate() {
    auto startTime = std::chrono::high_resolution_clock::now(); // debug
    delta = 4.0 / std::max(width, height) / zoom;
    threads.clear();
    for (unsigned int threadId = 0; threadId < numThreads; ++threadId) {
        const int startY = threadId * height / numThreads;
        const int endY = (threadId + 1) * height / numThreads;
        threads.emplace_back(&Mandelbrot::rowsGenerater, this, startY, endY);
    }
    for (auto& thread : threads)
        thread.join();
    elapsedTimes[0].push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startTime).count()); // debug
    return iterationData;
}
cv::Mat& Mandelbrot::render() {
    auto startTime = std::chrono::high_resolution_clock::now(); // debug
    threads.clear();
    for (unsigned int threadId = 0; threadId < numThreads; ++threadId) {
        const int startY = threadId * height / numThreads;
        const int endY = (threadId + 1) * height / numThreads;
        threads.emplace_back(&Mandelbrot::rowsColorRender, this, startY, endY);
    }
    for (auto& thread : threads) {
        thread.join();
    }
    elapsedTimes[1].push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startTime).count()); // debug
    startTime = std::chrono::high_resolution_clock::now(); // debug
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
    elapsedTimes[2].push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startTime).count()); // debug
    return image;
}