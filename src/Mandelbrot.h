#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>
#include <opencv2/core.hpp>
#include "ThreadPool.h"
#include "MandelbrotKernel.h"
class Mandelbrot {
public:
    // Work-splitting strategy. Tiled is the default; PerRow reproduces the old
    // one-task-per-row scheme and exists for benchmarking.
    enum class Schedule : std::uint8_t { Tiled, PerRow };
private:
    struct AlignedDelete {
        void operator()(double* ptr) const noexcept {
            operator delete(ptr, std::align_val_t(32));
        }
    };
    const int width;
    const int requested_width;
    const int height;
    const int max_iterations;
    double zoom = 1.0;
    double delta = 0.0;
    double offset_x = 0.0;
    double offset_y = 0.0;
    std::vector<std::array<uint8_t, 3>> color_map;
    std::unique_ptr<double, AlignedDelete> r_data;
    std::unique_ptr<double, AlignedDelete> i_data;
    ThreadPool thread_pool;
    cv::Mat image;
    RowKernel kernel_ = nullptr;
    const char* kernel_name_ = "unknown";
    Schedule schedule_ = Schedule::Tiled;
public:
    Mandelbrot(int _width = 1920, int _height = 1080, int _maxIterations = 2000);
    Mandelbrot(const Mandelbrot&) = delete;
    auto operator=(const Mandelbrot&) -> Mandelbrot& = delete;
    Mandelbrot(Mandelbrot&&) = delete;
    auto operator=(Mandelbrot&&) -> Mandelbrot& = delete;
    void setView(double new_zoom, double new_offset_x, double new_offset_y);
    void set_schedule(Schedule schedule) { schedule_ = schedule; }
    [[nodiscard]] auto kernel_name() const -> const char*;
private:
    void render_tile(int row_index, int x_begin, int x_end);
public:
    [[nodiscard]] auto generate() -> cv::Mat;
};
