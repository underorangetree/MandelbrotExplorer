#pragma once
#include <thread>
#include <vector>
#include <opencv2/opencv.hpp>
class Mandelbrot {
private:
    const int width;
    const int height;
    const int maxIterations;
    double zoom = 0.8;
    double delta;
    double offsetX = 0.0;
    double offsetY = 0.0;
    double* const rData;
    double* const iData;
    double* const iterationData;
    cv::Mat image;
    const unsigned int numThreads;
    std::vector<std::thread> threads;
public:
    std::vector<std::vector<long>> elapsedTimes; // debug
    Mandelbrot(int width, int height, int maxIterations) noexcept;
    ~Mandelbrot();
    void setView(double newZoom, double newOffsetX, double newOffsetY);
    double* getIterationData() const noexcept { return iterationData; }
private:
    void rowsGenerater(int startY, int endY);
    void rowsColorRender(int startY, int endY);
public:
    double* generate();
    cv::Mat& render();
};