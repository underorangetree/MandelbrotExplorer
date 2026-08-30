#if !defined(__AVX2__) && !defined(__ARM_NEON)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include "ExitStatus.h"
#include "Mandelbrot.h"

Mandelbrot::Mandelbrot(size_t _width, size_t _height, size_t _maxIterations):
    width(_width),
    height(_height),
    max_iterations(_maxIterations),
    r_data(static_cast<double*>(operator new(width * sizeof(double), std::align_val_t(32), std::nothrow))),
    i_data(static_cast<double*>(operator new(height * sizeof(double), std::align_val_t(32), std::nothrow))),
    thread_pool(height),
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
    for (size_t x = 0; x < width; ++x) {
        r_data[x] = std::fma(x - (width / 2.0), delta, offset_x);
    }
    for (size_t y = 0; y < height; ++y) {
        i_data[y] = std::fma(y - (height / 2.0), delta, offset_y);
    }
}

void Mandelbrot::row_task(size_t y) {
    uint8_t* const row_ptr = image.data + y * image.step;
    for (size_t x = 0; x < width; ++x) {
        uint8_t* const pixel_ptr = row_ptr + x * 3;
        double r = 0.0, i = 0.0;
        double ri = 0.0;
        double r2 = 0.0, i2 = 0.0;
        const double cr = r_data[x];
        const double ci = i_data[y];
        const double q = (cr - 0.25) * (cr - 0.25) + ci * ci;
        uint32_t iter = 0;
        if ((cr + 1.0) * (cr + 1.0) + ci * ci <= 1.0 / 16.0 ||
            q * (q + cr - 0.25) <= 0.25 * ci * ci) {
            iter = max_iterations;
        } else {
            while (r2 + i2 <= 4.0 && iter < max_iterations) {
                i = std::fma(2.0, ri, ci);
                r = r2 - i2 + cr;
                i2 = i * i;
                r2 = r * r;
                ri = r * i;
                ++iter;
            }
        }
        pixel_ptr[0] = color_map[iter][0];
        pixel_ptr[1] = color_map[iter][1];
        pixel_ptr[2] = color_map[iter][2];
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