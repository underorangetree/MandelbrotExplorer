#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cmath>
#include <cstddef>
#include <deque>
#include <exception>
#include <format>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include "ExitStatus.h"
#include "CommandLine.h"
#include "Mandelbrot.h"
#include "VideoCodec.h"
#include "ViewSequence.h"

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

volatile std::sig_atomic_t sigint_flag = 0;

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
    Mandelbrot mandelbrot(config.width, config.height, config.max_iterations, config.threads);
    if (config.kernel != "auto" && !mandelbrot.set_kernel(config.kernel.c_str())) {
        std::cerr << "Requested kernel '" << config.kernel << "' is not available on this CPU/build.\n";
        return static_cast<int>(ExitStatus::InvalidArgument);
    }
    const int fourcc = mandelbrot::video_codec_fourcc(config.codec);
    cv::VideoWriter writer(config.output_file, fourcc, config.fps, cv::Size(config.width, config.height));
    if (!writer.isOpened()) {
        std::cerr << "Could not open the output video file for write (codec: " << config.codec << ").\n";
        return static_cast<int>(ExitStatus::VideoWriterError);
    }
    if (config.quality >= 0 && !writer.set(cv::VIDEOWRITER_PROP_QUALITY, static_cast<double>(config.quality))) {
        std::cerr << "Warning: the " << config.codec << " encoder does not support --quality.\n";
    }
    std::cout << std::format("Video will be saved to {}. Using {} threads. Kernel: {}. Codec: {}.\n", config.output_file, mandelbrot.thread_count(), mandelbrot.kernel_name(), config.codec);

    // Render and encode form a producer/consumer pipeline: the main thread
    // renders frame N+1 into a free buffer while the writer thread encodes frame
    // N. A few buffers are enough to keep the encoder busy; the queue bounds how
    // many frames can be in flight, so memory stays flat if the encoder is slow.
    constexpr std::size_t frame_buffer_count = 3;
    struct FrameBuffer {
        cv::Mat storage;  // padded width, written by the kernels
        cv::Mat view;     // cropped to the requested width, handed to the encoder
    };
    std::vector<FrameBuffer> frame_buffers;
    frame_buffers.reserve(frame_buffer_count);
    std::deque<int> free_indices;
    for (std::size_t index = 0; index < frame_buffer_count; ++index) {
        FrameBuffer buffer;
        buffer.storage = cv::Mat(config.height, mandelbrot.storage_width(), CV_8UC3);
        buffer.view = buffer.storage(cv::Rect(0, 0, config.width, config.height));
        frame_buffers.push_back(std::move(buffer));
        free_indices.push_back(static_cast<int>(index));
    }

    std::mutex queue_mutex;
    std::condition_variable free_cv;
    std::condition_variable ready_cv;
    std::deque<int> ready_indices;
    bool pipeline_done = false;
    std::exception_ptr writer_error;

    // The writer is the only thread that touches the VideoWriter. It encodes
    // every queued frame before exiting and reports failures back to run().
    std::thread writer_thread([&] {
        for (;;) {
            int index = -1;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                ready_cv.wait(lock, [&] { return !ready_indices.empty() || pipeline_done; });
                if (ready_indices.empty()) {
                    return;
                }
                index = ready_indices.front();
                ready_indices.pop_front();
            }
            try {
                writer.write(frame_buffers[static_cast<std::size_t>(index)].view);
            } catch (...) {
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    writer_error = std::current_exception();
                    pipeline_done = true;
                }
                free_cv.notify_all();
                ready_cv.notify_all();
                return;
            }
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                free_indices.push_back(index);
            }
            free_cv.notify_one();
        }
    });

    const double total_frames = config.fps * config.duration_seconds;
    double zoom = NAN;
    double render_speed = 0.0;
    double progress_update_elapsed = 0;
    int frame = 0;
    auto render_start_time = std::chrono::steady_clock::now();
    try {
        for (; frame < total_frames; ++frame) {
            if (sigint_flag != 0) {
                break;
            }
            int index = -1;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                free_cv.wait(lock, [&] { return !free_indices.empty() || pipeline_done; });
                if (free_indices.empty()) {
                    break; // the writer thread reported an error
                }
                index = free_indices.front();
                free_indices.pop_front();
            }
            zoom = mandelbrot::animation_zoom(frame, total_frames, config.animation);
            mandelbrot.setView(zoom, config.animation.center_x, config.animation.center_y);
            TimePoint start = std::chrono::steady_clock::now();
            mandelbrot.generate_into(frame_buffers[static_cast<std::size_t>(index)].storage.data,
                                     frame_buffers[static_cast<std::size_t>(index)].storage.step);
            TimePoint end = std::chrono::steady_clock::now();
            double elapsed = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1'000'000.0;
            progress_update_elapsed += elapsed;
            if (elapsed > 0.0) {
                render_speed = (0.63 * render_speed) + (0.37 / elapsed);
            }
            if (progress_update_elapsed > progress_update_interval) {
                progress(static_cast<double>(frame) / total_frames);
                std::cout << std::format("Frame {:>3} rendered in {:#.4g} ms. Approximate speed {:#.4g} FPS.", frame, elapsed * 1000, render_speed) << std::flush;
                progress_update_elapsed = 0;
            }
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                ready_indices.push_back(index);
            }
            ready_cv.notify_one();
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            pipeline_done = true;
        }
        ready_cv.notify_all();
        writer_thread.join();
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        pipeline_done = true;
    }
    ready_cv.notify_all();
    writer_thread.join();
    if (writer_error != nullptr) {
        std::rethrow_exception(writer_error);
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
