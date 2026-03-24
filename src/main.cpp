#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include "Mandelbrot.h"

// TODO:
// - 配置文件
// - 线程模型优化
// - bloom stage优化（不重要）

using timePoint = std::chrono::time_point<std::chrono::system_clock>;

constexpr int WIDTH = 400;
constexpr int HEIGHT = 225;
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

#ifdef _WIN32
    #include <windows.h>
    static int get_terminal_width() {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(h, &csbi)) {
            return csbi.srWindow.Right - csbi.srWindow.Left + 1;
        }
        return 100;
    }
#else
    #include <sys/ioctl.h>
    #include <unistd.h>
    static int get_terminal_width() {
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
            return w.ws_col;
        }
        char *cols = getenv("COLUMNS");
        if (cols) return atoi(cols);
        return 100;
    }
#endif

static void progress(double progress) {
    int bar_width = std::max(0, get_terminal_width() - 70);
    if (bar_width <= 0) return;
    std::cout << "\r[";
    int pos = static_cast<int>(bar_width * progress);
    for (int i = 0; i < pos; ++i) std::cout << "=";
    if (pos < bar_width) std::cout << ">";
    for (int i = pos + 1; i < bar_width; ++i) std::cout << " ";
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
    std::cout << std::format("Video will be saved to {}. Using {} threads.\n", OUTPUT_FILE, std::max(1U, std::thread::hardware_concurrency()));
    double zoom;
    double render_speed = 0.0;
    double progress_update_elapsed = 0;
    auto render_start_time = std::chrono::system_clock::now();
    for (int frame = 0; frame < FPS * DURATION_SECONDS; ++frame) {
        zoom = 0.8 * std::pow(1.05, smooth(0.0, 280, frame, DURATION_SECONDS * FPS));
        mandelbrot.setView(zoom, -0.743643887037158704752191506114774, 0.131825904205311970493132056385139);
        timePoint start = std::chrono::system_clock::now();
        mandelbrot.generate();
        cv::Mat& image = mandelbrot.render();
        timePoint end = std::chrono::system_clock::now();
        double elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1'000'000.0;
        progress_update_elapsed += elapsed;
        render_speed = 0.63 * render_speed + 0.37 / elapsed;
        if (progress_update_elapsed > PROGRESS_UPDATE_INTERVAL) {
            progress(static_cast<double>(frame) / (FPS * DURATION_SECONDS));
            std::cout << std::format("Frame {:>3} rendered in {:#.4g} ms. Approximate speed {:#.4g} FPS.", frame, elapsed * 1000, render_speed) << std::flush;
            progress_update_elapsed = 0;
        }
        writer.write(image);
    }
    std::cout << "\r\033[2K";
    progress(1.0);
    writer.release();
    auto render_end_time = std::chrono::system_clock::now();
    auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(render_end_time - render_start_time).count();
    std::cout << std::format("\nTotal rendering time: {} ms. Average speed: {:.4g} FPS\n", totalElapsed, (FPS * DURATION_SECONDS) / (totalElapsed / 1000.0));

    // debug
    for (size_t i = 0; i < mandelbrot.elapsed_times.size(); ++i) {
        long sum = 0;
        for (long t : mandelbrot.elapsed_times[i]) sum += t;
        std::cout << std::format("Stage {} average time: {} microseconds\n", i, sum / static_cast<double>(mandelbrot.elapsed_times[i].size()));
    }
    std::ofstream file("timings.csv", std::ios::out);
    if (!file.is_open()) return -2;
    file << "Generate Stage, Color Stage, Bloom Stage\n";
    auto size = mandelbrot.elapsed_times[0].size();
    for (size_t i = 0; i < size; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            file << mandelbrot.elapsed_times[j][i];
            if (j != 2) file << ",";
        }
        file << "\n";
    }

    return 0;
}