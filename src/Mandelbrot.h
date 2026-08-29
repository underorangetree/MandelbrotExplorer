#pragma once
#include <cstddef>
#include <opencv2/opencv.hpp>
#include <vector>
#include "ThreadPool.h"
class Mandelbrot {
private:
    const size_t width;
    const size_t height;
    const size_t max_iterations;
    double zoom = 0.8;
    double delta;
    double offset_x = 0.0;
    double offset_y = 0.0;
    std::vector<std::array<uint8_t, 3>> color_map;
    double* const r_data;
    double* const i_data;

    ThreadPool thread_pool;
    cv::Mat image;
public:
    Mandelbrot(size_t _width = 1920, size_t _height = 1080, size_t _maxIterations = 1000);
    ~Mandelbrot();
    void setView(double new_zoom, double new_offset_x, double new_offset_y);
private:
    void row_task(size_t y);
public:
    [[__nodiscard__]] cv::Mat& generate();
};