#include <algorithm>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <opencv2/core.hpp>
#include "Mandelbrot.h"

namespace {

struct Measurement {
    double seconds_per_frame;
    unsigned long long checksum;
};

auto measure(Mandelbrot::Schedule schedule, int width, int height, int max_iterations,
             int frames, double zoom, double center_x, double center_y) -> Measurement {
    Mandelbrot mandelbrot(width, height, max_iterations);
    mandelbrot.set_schedule(schedule);
    mandelbrot.setView(zoom, center_x, center_y);
    mandelbrot.generate(); // warm-up

    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < frames; ++frame) {
        mandelbrot.setView(zoom, center_x, center_y);
        mandelbrot.generate();
    }
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();

    // Checksum computed outside the timed loop so it cannot skew the result.
    const cv::Mat image = mandelbrot.generate();
    const cv::Scalar sum = cv::sum(image);
    const auto checksum = static_cast<unsigned long long>(sum[0] + sum[1] + sum[2]);
    return {.seconds_per_frame=seconds / frames, .checksum=checksum};
}

auto smoothstep(double low, double high, double t, double t0) -> double {
    const double x = t / t0;
    const double weight = x * x * (3.0 - (2.0 * x));
    return low + ((high - low) * weight);
}

// Renders the same zoom animation the application produces, so the benchmark
// reflects the real workload distribution rather than a single fixed view.
auto measure_animation_once(Mandelbrot::Schedule schedule, int width, int height,
                            int max_iterations, int frames) -> double {
    constexpr double center_x = -0.743643887037158704752191506114774;
    constexpr double center_y = 0.131825904205311970493132056385139;
    Mandelbrot mandelbrot(width, height, max_iterations);
    mandelbrot.set_schedule(schedule);
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < frames; ++frame) {
        const double zoom = 0.8 * std::pow(1.05, smoothstep(0.0, 280.0, frame, frames));
        mandelbrot.setView(zoom, center_x, center_y);
        mandelbrot.generate();
    }
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(end - start).count();
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "anim") == 0) {
        const int width = argc > 2 ? std::atoi(argv[2]) : 1920;
        const int height = argc > 3 ? std::atoi(argv[3]) : 1080;
        const int max_iterations = argc > 4 ? std::atoi(argv[4]) : 2000;
        const int frames = argc > 5 ? std::atoi(argv[5]) : 120;
        const int repetitions = argc > 6 ? std::atoi(argv[6]) : 3;
        const Mandelbrot::Schedule schedules[2] = {Mandelbrot::Schedule::PerRow, Mandelbrot::Schedule::Tiled};
        const char* const names[2] = {"per-row", "tiled"};
        double best[2] = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        for (int rep = 0; rep < repetitions; ++rep) {
            for (int index = 0; index < 2; ++index) {
                const int slot = (rep % 2 == 0) ? index : 1 - index;
                const double seconds = measure_animation_once(schedules[slot], width, height, max_iterations, frames);
                best[slot] = std::min(best[slot], seconds);
            }
        }
        std::printf("animation %dx%d, maxiter %d, %d frames, reps %d\n", width, height, max_iterations, frames, repetitions);
        for (int index = 0; index < 2; ++index) {
            std::printf("  %-8s total: %.3f s  (%.2f ms/frame avg)\n", names[index], best[index], best[index] / frames * 1000.0);
        }
        std::printf("  speedup (per-row/tiled): %.3fx\n", best[0] / best[1]);
        return 0;
    }
    const int width = argc > 1 ? std::atoi(argv[1]) : 1920;
    const int height = argc > 2 ? std::atoi(argv[2]) : 1080;
    const int max_iterations = argc > 3 ? std::atoi(argv[3]) : 2000;
    const int frames = argc > 4 ? std::atoi(argv[4]) : 20;
    const double zoom = argc > 5 ? std::atof(argv[5]) : 1.0;
    const double center_x = argc > 6 ? std::atof(argv[6]) : -0.5;
    const double center_y = argc > 7 ? std::atof(argv[7]) : 0.0;
    constexpr int repetitions = 5;

    const Mandelbrot::Schedule schedules[2] = {Mandelbrot::Schedule::PerRow, Mandelbrot::Schedule::Tiled};
    const char* const names[2] = {"per-row", "tiled"};
    double best[2] = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    unsigned long long checksum[2] = {0, 0};

    // Alternate the order across repetitions so frequency/thermal drift does
    // not systematically favour one schedule.
    for (int rep = 0; rep < repetitions; ++rep) {
        for (int index = 0; index < 2; ++index) {
            const int slot = (rep % 2 == 0) ? index : 1 - index;
            const Measurement result = measure(schedules[slot], width, height, max_iterations, frames, zoom, center_x, center_y);
            best[slot] = std::min(result.seconds_per_frame, best[slot]);
            checksum[slot] = result.checksum;
        }
    }

    std::printf("resolution %dx%d, maxiter %d, frames %d, zoom %g, center (%g, %g), reps %d\n",
                width, height, max_iterations, frames, zoom, center_x, center_y, repetitions);
    for (int index = 0; index < 2; ++index) {
        std::printf("  %-8s: %8.2f ms/frame  (%7.2f FPS)  checksum %llu\n",
                    names[index], best[index] * 1000.0, 1.0 / best[index], checksum[index]);
    }
    std::printf("  speedup (per-row/tiled): %.3fx\n", best[0] / best[1]);
    if (checksum[0] != checksum[1]) {
        std::printf("  WARNING: checksums differ, schedules produce different output!\n");
    }
    return 0;
}
