#include <algorithm>
#include <chrono>
#include <csignal>
#include <cmath>
#include <exception>
#include <format>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include "ExitStatus.h"
#include "CommandLine.h"
#include "Mandelbrot.h"

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

volatile std::sig_atomic_t sigint_flag = 0;

static auto smooth(double a, double b, double t, double t0) -> double {
    double x = t / t0;
    double smooth = x * x * (3 - (2 * x));
    return a + ((b - a) * smooth);
}

constexpr int default_terminal_width = 100;
#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#include <windows.h>
    static auto get_terminal_width() -> int {
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(handle, &csbi) != 0) {
            return csbi.srWindow.Right - csbi.srWindow.Left + 1;
        }
        return default_terminal_width;
    }
#else
    #include <sys/ioctl.h>
    #include <unistd.h>
    static auto get_terminal_width() -> int {
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
            return w.ws_col;
        }
        char* cols = getenv("COLUMNS");
        if (cols) return atoi(cols);
        return default_terminal_width;
    }
#endif

static auto progress(double progress) -> void {
    constexpr int min_bar_width = 70;
    int bar_width = std::max(0, get_terminal_width() - min_bar_width);
    if (bar_width <= 0) {
        return;
    }
    std::cout << "\r[";
    int pos = static_cast<int>(bar_width * progress);
    for (int i = 0; i < pos; ++i) {
        std::cout << "=";
    }
    if (pos < bar_width) {
        std::cout << ">";
    }
    for (int i = pos + 1; i < bar_width; ++i) {
        std::cout << " ";
    }
    std::cout << std::format("] {:>3} % ", static_cast<int>(progress * 100.0));
}

static void sigint_handler(int) {
    sigint_flag = 1;
}

static auto run(const RenderConfig& config, double progress_update_interval) -> int {
    std::signal(SIGINT, sigint_handler);
    Mandelbrot mandelbrot(config.width, config.height, config.max_iterations);
    auto fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    cv::VideoWriter writer(config.output_file, fourcc, config.fps, cv::Size(config.width, config.height));
    if (!writer.isOpened()) {
        std::cerr << "Could not open the output video file for write.\n";
        return static_cast<int>(ExitStatus::VideoWriterError);
    }
    std::cout << std::format("Video will be saved to {}. Using {} threads. Kernel: {}.\n", config.output_file, std::max(1U, std::thread::hardware_concurrency()), mandelbrot.kernel_name());
    double zoom = NAN;
    double render_speed = 0.0;
    double progress_update_elapsed = 0;
    int frame = 0;
    auto render_start_time = std::chrono::steady_clock::now();
    for (; frame < config.fps * config.duration_seconds; ++frame) {
        if (sigint_flag != 0) {
            break;
        }
        zoom = 0.8 * std::pow(1.05, smooth(0.0, 280, frame, config.duration_seconds * config.fps));
        mandelbrot.setView(zoom, -0.743643887037158704752191506114774, 0.131825904205311970493132056385139);
        TimePoint start = std::chrono::steady_clock::now();
        cv::Mat image = mandelbrot.generate();
        TimePoint end = std::chrono::steady_clock::now();
        double elapsed = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1'000'000.0;
        progress_update_elapsed += elapsed;
        if (elapsed > 0.0) {
            render_speed = (0.63 * render_speed) + (0.37 / elapsed);
        }
        if (progress_update_elapsed > progress_update_interval) {
            progress(static_cast<double>(frame) / (config.fps * config.duration_seconds));
            std::cout << std::format("Frame {:>3} rendered in {:#.4g} ms. Approximate speed {:#.4g} FPS.", frame, elapsed * 1000, render_speed) << std::flush;
            progress_update_elapsed = 0;
        }
        writer.write(image);
    }
    std::cout << "\r\033[2K";
    if (sigint_flag != 0) {
        std::cout << "Interrupt signal received. Exiting...\n";
    } else {
        progress(1.0);
    }
    writer.release();
    auto render_end_time = std::chrono::steady_clock::now();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(render_end_time - render_start_time).count();
    const double average_fps = (total_elapsed > 0)
        ? static_cast<double>(frame) / (static_cast<double>(total_elapsed) / 1000.0)
        : 0.0;
    std::cout << std::format("\nTotal rendering time: {} ms. Average speed: {:.4g} FPS\n", total_elapsed, average_fps);
    return 0;
}

auto main(int argc, char* argv[]) -> int {
    RenderConfig config;
    constexpr double progress_update_interval = 0.5;
    try {
        if (auto result = parse_command_line({argv, static_cast<size_t>(argc)}, config)) {
            return static_cast<int>(*result);
        }
        return run(config, progress_update_interval);
    } catch (const std::invalid_argument& error) {
        std::cerr << "Invalid argument: " << error.what() << "\n";
        return static_cast<int>(ExitStatus::InvalidArgument);
    } catch (const cv::Exception& error) {
        std::cerr << "OpenCV error: " << error.what() << "\n";
        return static_cast<int>(ExitStatus::RuntimeError);
    } catch (const std::bad_alloc&) {
        std::cerr << "Memory allocation failed.\n";
        return static_cast<int>(ExitStatus::MemoryAllocationError);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return static_cast<int>(ExitStatus::RuntimeError);
    }
}
