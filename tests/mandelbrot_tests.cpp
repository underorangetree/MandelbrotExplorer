#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <opencv2/core.hpp>
#include "CommandLine.h"
#include "ExitStatus.h"
#include "Mandelbrot.h"
#include "MandelbrotKernel.h"
#include "ThreadPool.h"

namespace {

auto parse(std::vector<std::string>& storage, RenderConfig& config) -> std::optional<ExitStatus> {
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& argument : storage) {
        argv.push_back(argument.data());
    }
    return parse_command_line({argv.data(), argv.size()}, config);
}

void test_mandelbrot_known_points() {
    Mandelbrot mandelbrot(16, 16, 64);
    mandelbrot.setView(1.0, 0.0, 0.0);
    cv::Mat image = mandelbrot.generate();
    assert(image.cols == 16 && image.rows == 16);
    assert(image.at<cv::Vec3b>(8, 8) == cv::Vec3b(0, 0, 0));   // (0,0) is in the set
    assert(image.at<cv::Vec3b>(8, 12) != cv::Vec3b(0, 0, 0));  // (1,0) escapes
}

void test_crop_to_requested_width() {
    Mandelbrot mandelbrot(17, 16, 64);
    cv::Mat image = mandelbrot.generate();
    assert(image.cols == 17 && image.rows == 16);
}

void test_schedule_equivalence() {
    Mandelbrot tiled(256, 32, 64);
    Mandelbrot per_row(256, 32, 64);
    tiled.setView(1.5, -0.5, 0.0);
    per_row.setView(1.5, -0.5, 0.0);
    tiled.set_schedule(Mandelbrot::Schedule::Tiled);
    per_row.set_schedule(Mandelbrot::Schedule::PerRow);
    const cv::Mat a = tiled.generate();
    const cv::Mat b = per_row.generate();
    assert(a.size() == b.size());
    assert(cv::norm(a, b, cv::NORM_INF) == 0.0);
}

void test_thread_pool() {
    ThreadPool pool(8);
    std::atomic<int> count{0};
    for (int i = 0; i < 100; ++i) {
        pool.enqueue([&count] { ++count; });
    }
    pool.wait_all_idle();
    assert(count == 100);
}

void test_kernel_selection() {
    const KernelSelection selection = select_kernel();
    assert(selection.function != nullptr);
    assert(selection.name != nullptr);
    Mandelbrot mandelbrot(8, 8, 16);
    assert(mandelbrot.kernel_name() != nullptr);
}

void test_command_line_success() {
    RenderConfig config;
    std::vector<std::string> args = {"prog", "--size", "100x50", "--maxiter", "10", "--fps", "2", "--duration", "3", "-o", "out.mp4"};
    auto result = parse(args, config);
    assert(!result.has_value());
    assert(config.width == 100);
    assert(config.height == 50);
    assert(config.max_iterations == 10);
    assert(config.fps == 2.0);
    assert(config.duration_seconds == 3.0);
    assert(config.output_file == "out.mp4");
}

void test_command_line_help() {
    RenderConfig config;
    std::vector<std::string> args = {"prog", "--help"};
    auto result = parse(args, config);
    assert(result.has_value() && *result == ExitStatus::Success);
}

void test_command_line_errors() {
    const std::vector<std::vector<std::string>> invalid = {
        {"prog", "--size", "0x0"},
        {"prog", "--size", "1920"},
        {"prog", "--size", "1920x"},
        {"prog", "--size", "1920x1080x5"},
        {"prog", "--width", "-5"},
        {"prog", "--fps", "0"},
        {"prog", "--fps", "nan"},
        {"prog", "--duration", "-1"},
        {"prog", "--maxiter", "abc"},
        {"prog", "-o", ""},
        {"prog", "--unknown"},
        {"prog", "--fps", "0.1", "--duration", "1"},
        {"prog", "--width", "32769"},
        {"prog", "--height", "0"},
        {"prog", "--maxiter", "0"},
        {"prog", "--maxiter", "1000001"},
        {"prog", "--fps", "1001"},
        {"prog", "--fps", "0.0005"},
        {"prog", "--duration", "86401"},
        {"prog", "--duration", "0.0005"},
        {"prog", "--size", "1920X1080"},
        {"prog", "--size", "32769x1080"},
        {"prog", "--size", "10x-10"},
        {"prog", "--width", "abc"}
    };
    for (const auto& args : invalid) {
        RenderConfig config;
        std::vector<std::string> mutable_args = args;
        auto result = parse(mutable_args, config);
        assert(result.has_value() && *result == ExitStatus::InvalidArgument);
    }
}

void test_command_line_boundaries_accepted() {
    const std::vector<std::vector<std::string>> valid = {
        {"prog", "--size", "32768x1", "--maxiter", "1000000", "--fps", "1000", "--duration", "86400"},
        {"prog", "--size", "1x32768", "--fps", "0.001", "--duration", "2000"},
        {"prog", "--maxiter", "1"},
        {"prog", "--width", "32768"},
        {"prog", "--height", "32768"}
    };
    for (const auto& args : valid) {
        RenderConfig config;
        std::vector<std::string> mutable_args = args;
        auto result = parse(mutable_args, config);
        assert(!result.has_value());
    }
}

struct AlignedDelete {
    void operator()(double* ptr) const noexcept {
        operator delete(ptr, std::align_val_t(32));
    }
};

// Renders the same rows with the scalar kernel and with the CPU-selected kernel
// and compares the buffers byte for byte. The color map encodes the iteration
// count, so any difference in the escape-time result shows up as a byte diff.
void test_kernel_matches_scalar() {
    constexpr int width = 32;
    constexpr int height = 8;
    constexpr int max_iterations = 200;
    constexpr double delta = 4.0 / (width > height ? width : height) / 1.5;

    // The AVX2 kernel uses aligned loads, so r_data must be 32-byte aligned.
    const std::unique_ptr<double, AlignedDelete> r_data(
        static_cast<double*>(operator new(sizeof(double) * width, std::align_val_t(32))));
    std::vector<double> i_data(height);
    std::vector<std::array<uint8_t, 3>> color_map(max_iterations + 1);
    std::vector<uint8_t> scalar_buffer(static_cast<size_t>(width) * height * 3);
    std::vector<uint8_t> kernel_buffer(static_cast<size_t>(width) * height * 3);

    for (int col = 0; col < width; ++col) {
        r_data.get()[col] = (col - width / 2) * delta - 0.5;
    }
    for (int row = 0; row < height; ++row) {
        i_data[row] = (row - height / 2) * delta;
    }
    for (int iter = 0; iter <= max_iterations; ++iter) {
        color_map[iter] = {static_cast<uint8_t>(iter & 0xFF),
                           static_cast<uint8_t>((iter >> 8) & 0xFF),
                           static_cast<uint8_t>((iter >> 16) & 0xFF)};
    }

    const RowKernel kernel = select_kernel().function;
    for (int row = 0; row < height; ++row) {
        const RowContext scalar_context{r_data.get(), i_data[row],
                                        scalar_buffer.data() + static_cast<size_t>(row) * width * 3,
                                        0, width, max_iterations, color_map.data()};
        render_row_scalar(scalar_context);
        const RowContext kernel_context{r_data.get(), i_data[row],
                                        kernel_buffer.data() + static_cast<size_t>(row) * width * 3,
                                        0, width, max_iterations, color_map.data()};
        kernel(kernel_context);
    }
    assert(scalar_buffer == kernel_buffer);
}

void test_mandelbrot_constructor_validation() {
    bool threw = false;
    try {
        Mandelbrot invalid_max_iter(16, 16, 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        Mandelbrot invalid_width(0, 16, 16);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        Mandelbrot invalid_height(16, 0, 16);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        Mandelbrot invalid_max_iter_range(16, 16, 1000001);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        Mandelbrot invalid_width_range(32769, 16, 16);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

} // namespace

auto main() -> int {
    test_mandelbrot_known_points();
    test_crop_to_requested_width();
    test_schedule_equivalence();
    test_thread_pool();
    test_kernel_selection();
    test_command_line_success();
    test_command_line_help();
    test_command_line_errors();
    test_command_line_boundaries_accepted();
    test_kernel_matches_scalar();
    test_mandelbrot_constructor_validation();
    return 0;
}
