#pragma once
#include <thread>
#include <vector>
#include <opencv2/opencv.hpp>
class Mandelbrot {
private:
    const int width;
    const int height;
    const int max_iterations;
    double zoom = 0.8;
    double delta;
    double offsetX = 0.0;
    double offsetY = 0.0;
    double* const r_data;
    double* const i_data;
    float* const iteration_data;
    cv::Mat image;
    const unsigned int num_threads;
    std::vector<std::thread> threads;
    bool pass_bloom_stage = false;
public:
    std::vector<std::vector<long>> elapsed_times; // debug
    Mandelbrot(int _width, int _height, int _maxIterations) noexcept;
    ~Mandelbrot();
    void setView(double new_zoom, double new_offset_x, double new_offset_y);
    float* getIteration_data() const noexcept { return iteration_data; }
private:
    void rows_generate(int startY, int endY);
    void rows_color_render(int startY, int endY);
public:
    void generate();
    cv::Mat& render();
};