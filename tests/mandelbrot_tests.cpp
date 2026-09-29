#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

// Always-on check: unlike CHECK(), it fires even with NDEBUG defined, so the
// tests cannot silently become no-ops in a Release build.
#define CHECK(condition)                                                                        \
    do {                                                                                        \
        if (!(condition)) {                                                                     \
            std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            std::abort();                                                                       \
        }                                                                                       \
    } while (false)

namespace {

auto parse(std::vector<std::string>& storage, RenderConfig& config) -> std::optional<ExitStatus> {
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& argument : storage) {
        argv.push_back(argument.data());
    }
    return parse_command_line({argv.data(), argv.size()}, config);
}

// Escape-time checks for two known points, independent of the color map: the
// color entries encode the iteration count.
void test_known_point_iterations() {
    constexpr int max_iterations = 128;
    const double real_parts[2] = {0.0, 1.0};
    std::vector<std::array<uint8_t, 3>> color_map(max_iterations + 1);
    for (int iter = 0; iter <= max_iterations; ++iter) {
        color_map[iter] = {static_cast<uint8_t>(iter & 0xFF),
                           static_cast<uint8_t>((iter >> 8) & 0xFF),
                           static_cast<uint8_t>((iter >> 16) & 0xFF)};
    }
    std::vector<uint8_t> buffer(2 * 3);
    const RowContext context{.r_data=real_parts, .ci=0.0, .row_ptr=buffer.data(), .x_begin=0, .x_end=2, .max_iterations=max_iterations, .color_map=color_map.data()};
    render_row_scalar(context);
    const auto iteration_at = [&buffer](int index) -> int {
        return static_cast<int>(buffer[index * 3]) |
               (static_cast<int>(buffer[index * 3 + 1]) << 8) |
               (static_cast<int>(buffer[index * 3 + 2]) << 16);
    };
    CHECK(iteration_at(0) == max_iterations); // (0,0) is inside the set
    CHECK(iteration_at(1) > 0);               // (1,0) escapes
    CHECK(iteration_at(1) < 10);              // ... quickly
}

void test_mandelbrot_known_points() {
    Mandelbrot mandelbrot(16, 16, 64);
    mandelbrot.setView(1.0, 0.0, 0.0);
    cv::Mat image = mandelbrot.generate();
    CHECK(image.cols == 16 && image.rows == 16);
    CHECK(image.at<cv::Vec3b>(8, 8) == cv::Vec3b(0, 0, 0)); // (0,0) is in the set; interior is black
}

void test_crop_to_requested_width() {
    Mandelbrot mandelbrot(17, 16, 64);
    cv::Mat image = mandelbrot.generate();
    CHECK(image.cols == 17 && image.rows == 16);
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
    CHECK(a.size() == b.size());
    CHECK(cv::norm(a, b, cv::NORM_INF) == 0.0);
}

void test_thread_pool() {
    ThreadPool pool(8);
    std::atomic<int> count{0};
    for (int i = 0; i < 100; ++i) {
        pool.enqueue([&count] { ++count; });
    }
    pool.wait_all_idle();
    CHECK(count == 100);
}

void test_kernel_selection() {
    const KernelSelection selection = select_kernel();
    CHECK(selection.function != nullptr);
    CHECK(selection.name != nullptr);
    // The scalar kernel is always available; "auto" resolves to the best kernel
    // for this CPU; unknown names are rejected.
    CHECK(find_kernel("scalar").function != nullptr);
    CHECK(find_kernel("auto").function != nullptr);
    CHECK(find_kernel("bogus").function == nullptr);
    Mandelbrot mandelbrot(8, 8, 16);
    CHECK(mandelbrot.kernel_name() != nullptr);
    CHECK(mandelbrot.set_kernel("scalar"));
    CHECK(!mandelbrot.set_kernel("bogus"));
}

void test_command_line_success() {
    RenderConfig config;
    CHECK(config.kernel == "auto"); // default
    CHECK(config.codec == "mp4v");
    CHECK(config.quality == -1);
    std::vector<std::string> args = {"prog", "--size", "100x50", "--maxiter", "10", "--fps", "2", "--duration", "3", "-o", "out.mp4", "--kernel", "scalar", "--threads", "3", "--codec", "h264", "--quality", "80"};
    auto result = parse(args, config);
    CHECK(!result.has_value());
    CHECK(config.kernel == "scalar");
    CHECK(config.threads == 3);
    CHECK(config.codec == "h264");
    CHECK(config.quality == 80);
    CHECK(config.width == 100);
    CHECK(config.height == 50);
    CHECK(config.max_iterations == 10);
    CHECK(config.fps == 2.0);
    CHECK(config.duration_seconds == 3.0);
    CHECK(config.output_file == "out.mp4");
}

void test_command_line_help() {
    RenderConfig config;
    std::vector<std::string> args = {"prog", "--help"};
    auto result = parse(args, config);
    CHECK(result.has_value() && *result == ExitStatus::Success);
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
        {"prog", "--width", "abc"},
        {"prog", "--kernel", "bogus"},
        {"prog", "--threads", "-1"},
        {"prog", "--threads", "abc"},
        {"prog", "--threads", "1025"},
        {"prog", "--codec", "bogus"},
        {"prog", "--quality", "101"},
        {"prog", "--quality", "-1"},
        {"prog", "--quality", "abc"}
    };
    for (const auto& args : invalid) {
        RenderConfig config;
        std::vector<std::string> mutable_args = args;
        auto result = parse(mutable_args, config);
        CHECK(result.has_value() && *result == ExitStatus::InvalidArgument);
    }
}

void test_command_line_boundaries_accepted() {
    const std::vector<std::vector<std::string>> valid = {
        {"prog", "--size", "32768x1", "--maxiter", "1000000", "--fps", "1000", "--duration", "86400"},
        {"prog", "--size", "1x32768", "--fps", "0.001", "--duration", "2000"},
        {"prog", "--maxiter", "1"},
        {"prog", "--width", "32768"},
        {"prog", "--height", "32768"},
        {"prog", "--kernel", "auto"},
        {"prog", "--threads", "0"},
        {"prog", "--threads", "1024"},
        {"prog", "--codec", "mp4v"},
        {"prog", "--codec", "mjpg"},
        {"prog", "--quality", "0"},
        {"prog", "--quality", "100"}
    };
    for (const auto& args : valid) {
        RenderConfig config;
        std::vector<std::string> mutable_args = args;
        auto result = parse(mutable_args, config);
        CHECK(!result.has_value());
    }
}

struct AlignedDelete {
    void operator()(double* ptr) const noexcept {
        operator delete(ptr, std::align_val_t(32));
    }
};

// Renders the same rows with the scalar kernel and with every kernel available
// in this build and supported by the CPU, then compares the buffers byte for
// byte. The color map encodes the iteration count, so any difference in the
// escape-time result shows up as a byte diff.
void test_kernel_matches_scalar() {
    constexpr int width = 32;
    constexpr int height = 8;
    constexpr int max_iterations = 200;
    constexpr double delta = 4.0 / (width > height ? width : height) / 1.5;

    // The vector kernels use aligned loads, so r_data must be 32-byte aligned.
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

    for (int row = 0; row < height; ++row) {
        const RowContext context{.r_data=r_data.get(), .ci=i_data[row],
                                 .row_ptr=scalar_buffer.data() + static_cast<size_t>(row) * width * 3,
                                 .x_begin=0, .x_end=width, .max_iterations=max_iterations, .color_map=color_map.data()};
        render_row_scalar(context);
    }

    for (const char* name : {"avx512", "avx2", "neon"}) {
        const KernelSelection selection = find_kernel(name);
        if (selection.function == nullptr) {
            continue; // not compiled into this build, or unsupported by this CPU
        }
        for (int row = 0; row < height; ++row) {
            const RowContext context{.r_data=r_data.get(), .ci=i_data[row],
                                     .row_ptr=kernel_buffer.data() + static_cast<size_t>(row) * width * 3,
                                     .x_begin=0, .x_end=width, .max_iterations=max_iterations, .color_map=color_map.data()};
            selection.function(context);
        }
        CHECK(kernel_buffer == scalar_buffer);
    }
}

// Every kernel must produce the same image through the full framework
// (coordinate setup, padding, scheduling, cropping) as the scalar kernel.
void test_forced_kernel_rendering() {
    Mandelbrot automatic(64, 32, 64);
    automatic.setView(1.0, 0.0, 0.0);
    const cv::Mat reference = automatic.generate();

    Mandelbrot forced_scalar(64, 32, 64);
    forced_scalar.setView(1.0, 0.0, 0.0);
    CHECK(forced_scalar.set_kernel("scalar"));
    CHECK(std::string(forced_scalar.kernel_name()) == "scalar");
    CHECK(cv::norm(forced_scalar.generate(), reference, cv::NORM_INF) == 0.0);

    for (const char* name : {"avx512", "avx2", "neon"}) {
        if (find_kernel(name).function == nullptr) {
            continue;
        }
        Mandelbrot forced(64, 32, 64);
        forced.setView(1.0, 0.0, 0.0);
        CHECK(forced.set_kernel(name));
        CHECK(std::string(forced.kernel_name()) == name);
        CHECK(cv::norm(forced.generate(), reference, cv::NORM_INF) == 0.0);
    }
}

// generate_into() must render the same pixels as generate() into a
// caller-provided storage buffer, and must reject buffers the kernels would
// write past (wrong type, too few rows, narrower than the padded width).
void test_generate_into() {
    Mandelbrot mandelbrot(64, 32, 64);
    mandelbrot.setView(1.0, -0.5, 0.0);
    const cv::Mat reference = mandelbrot.generate();

    cv::Mat storage(32, mandelbrot.storage_width(), CV_8UC3);
    mandelbrot.generate_into(storage);
    CHECK(cv::norm(storage(cv::Rect(0, 0, 64, 32)), reference, cv::NORM_INF) == 0.0);

    cv::Mat wider(32, mandelbrot.storage_width() + 32, CV_8UC3);
    mandelbrot.generate_into(wider);
    CHECK(cv::norm(wider(cv::Rect(0, 0, 64, 32)), reference, cv::NORM_INF) == 0.0);

    bool threw = false;
    try {
        cv::Mat wrong_type(32, mandelbrot.storage_width(), CV_8UC1);
        mandelbrot.generate_into(wrong_type);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        cv::Mat too_narrow(32, mandelbrot.storage_width() - 1, CV_8UC3);
        mandelbrot.generate_into(too_narrow);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        cv::Mat wrong_height(31, mandelbrot.storage_width(), CV_8UC3);
        mandelbrot.generate_into(wrong_height);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

// Regression: GCC/Clang may contract a*b+c into an FMA in one kernel but not in
// another, which changes the rounding of a few boundary pixels. These rows come
// from the default 1280x720 view at zoom 1.0 and contain pixels that diverge
// from the scalar reference when the AVX-512 kernel is compiled with FP
// contraction enabled.
void test_kernel_rounding_ties() {
    constexpr int width = 1280;
    constexpr int height = 720;
    constexpr int max_iterations = 1000;
    constexpr double delta = 4.0 / width;
    constexpr int rows[] = {90, 93, 145, 160, 170, 172, 173, 181, 183, 184};

    const std::unique_ptr<double, AlignedDelete> r_data(
        static_cast<double*>(operator new(sizeof(double) * width, std::align_val_t(32))));
    std::vector<std::array<uint8_t, 3>> color_map(max_iterations + 1);
    std::vector<uint8_t> scalar_buffer(static_cast<size_t>(width) * 3);
    std::vector<uint8_t> kernel_buffer(static_cast<size_t>(width) * 3);

    for (int col = 0; col < width; ++col) {
        r_data.get()[col] = std::fma(col - (width / 2), delta, -0.5);
    }
    for (int iter = 0; iter <= max_iterations; ++iter) {
        color_map[iter] = {static_cast<uint8_t>(iter & 0xFF),
                           static_cast<uint8_t>((iter >> 8) & 0xFF),
                           static_cast<uint8_t>((iter >> 16) & 0xFF)};
    }

    for (const int row : rows) {
        const RowContext context{.r_data=r_data.get(),
                                 .ci=std::fma(row - (height / 2), delta, 0.0),
                                 .row_ptr=scalar_buffer.data(),
                                 .x_begin=0,
                                 .x_end=width,
                                 .max_iterations=max_iterations,
                                 .color_map=color_map.data()};
        render_row_scalar(context);
        for (const char* name : {"avx512", "avx2", "neon"}) {
            const KernelSelection selection = find_kernel(name);
            if (selection.function == nullptr) {
                continue; // not compiled into this build, or unsupported by this CPU
            }
            RowContext kernel_context = context;
            kernel_context.row_ptr = kernel_buffer.data();
            selection.function(kernel_context);
            CHECK(kernel_buffer == scalar_buffer);
        }
    }
}

void test_mandelbrot_constructor_validation() {
    bool threw = false;
    try {
        Mandelbrot invalid_max_iter(16, 16, 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        Mandelbrot invalid_width(0, 16, 16);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        Mandelbrot invalid_height(16, 0, 16);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        Mandelbrot invalid_max_iter_range(16, 16, 1000001);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        Mandelbrot invalid_width_range(32769, 16, 16);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        Mandelbrot invalid_threads_negative(16, 16, 16, -1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        Mandelbrot invalid_threads_range(16, 16, 16, 1025);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    Mandelbrot threaded(16, 16, 16, 3);
    CHECK(threaded.thread_count() == 3);
}

} // namespace

auto main() -> int {
    test_known_point_iterations();
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
    test_forced_kernel_rendering();
    test_generate_into();
    test_kernel_rounding_ties();
    test_mandelbrot_constructor_validation();
    return 0;
}
