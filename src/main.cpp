#include <chrono>
#include <format>
#include <fstream>
#include "Mandelbrot.h"

using timePoint = std::chrono::time_point<std::chrono::system_clock>;

constexpr int WIDTH = 1920;
constexpr int HEIGHT = 1080;
constexpr int MAX_ITER = 1000;
constexpr int FPS = 60;
constexpr int DURATION_SECONDS = 10;
constexpr char OUTPUT_FILE[] = "mandelbrot.mp4";
constexpr double PROGRESS_UPDATE_INTERVAL = 0.5;

static double smooth(double a, double b, double t, double t0) {
    double x = t / t0;
    double smooth = x * x * (3 - 2 * x);
    return a + (b - a) * smooth;
}

static void progress(double progress) {
    int barWidth = 50;
    std::cout << "\r[";
    int pos = static_cast<int>(barWidth * progress);
    for (int i = 0; i < pos; ++i) std::cout << "=";
    if (pos < barWidth) std::cout << ">";
    for (int i = pos + 1; i < barWidth; ++i) std::cout << " ";
    std::cout << std::format("] {:>3} % ", static_cast<int>(progress * 100.0));
}

int main() {
    Mandelbrot mandelbrot(WIDTH, HEIGHT, MAX_ITER);
    auto fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    cv::VideoWriter writer(OUTPUT_FILE, fourcc, FPS, cv::Size(WIDTH, HEIGHT));
    if (!writer.isOpened()) {
        std::cerr << "Could not open the output video file for write.\n";
        return -1;
    }
    std::cout << std::format("Video will be saved to {}.\n", OUTPUT_FILE);
    double zoom;
    double renderSpeed = 0.0;
    double progressUpdateElapsed = 0;
    auto renderStartTime = std::chrono::system_clock::now();
    for (int frame = 0; frame < FPS * DURATION_SECONDS; ++frame) {
        zoom = 0.8 * std::pow(1.05, smooth(0.0, 280, frame, DURATION_SECONDS * FPS));
        mandelbrot.setView(zoom, -0.743643887037158704752191506114774, 0.131825904205311970493132056385139);
        timePoint start = std::chrono::system_clock::now();
        mandelbrot.generate();
        cv::Mat image = mandelbrot.render();
        timePoint end = std::chrono::system_clock::now();
        double elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1'000'000.0;
        progressUpdateElapsed += elapsed;
        renderSpeed = 0.63 * renderSpeed + 0.37 / elapsed;
        if (progressUpdateElapsed > PROGRESS_UPDATE_INTERVAL) {
            progress(static_cast<double>(frame) / (FPS * DURATION_SECONDS));
            std::cout << std::format("Frame {:>3} rendered in {:#.4g} ms. Approximate speed {:#.4g} FPS.", frame, elapsed * 1000, renderSpeed) << std::flush;
            progressUpdateElapsed = 0;
        }
        writer.write(image);
    }
    std::cout << "\r\033[2K";
    progress(1.0);
    writer.release();
    auto renderEndTime = std::chrono::system_clock::now();
    auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(renderEndTime - renderStartTime).count();
    std::cout << std::format("\nTotal rendering time: {} ms. Average speed: {:.4g} FPS\n", totalElapsed, (FPS * DURATION_SECONDS) / (totalElapsed / 1000.0));

    // debug
    for (size_t i = 0; i < mandelbrot.elapsedTimes.size(); ++i) {
        long sum = 0;
        for (long t : mandelbrot.elapsedTimes[i]) sum += t;
        std::cout << std::format("Stage {} average time: {:.4g} microseconds\n", i, sum / static_cast<double>(mandelbrot.elapsedTimes[i].size()));
    }
    std::ofstream file("timings.csv", std::ios::out);
    if (!file.is_open()) return -2;
    file << "Generate Stage, Color Stage, Bloom Stage\n";
    auto size = mandelbrot.elapsedTimes[0].size();
    for (size_t i = 0; i < size; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            file << mandelbrot.elapsedTimes[j][i];
            if (j != 2) file << ",";
        }
        file << "\n";
    }

    return 0;
}