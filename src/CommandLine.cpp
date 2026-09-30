#include "CommandLine.h"
#include "MandelbrotLimits.h"
#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <unordered_map>

#ifndef MANDELBROT_EXPLORER_VERSION
#define MANDELBROT_EXPLORER_VERSION "unknown"
#endif

namespace {

constexpr int min_dimension = mandelbrot::min_dimension;
constexpr int max_dimension = mandelbrot::max_dimension;
constexpr int min_max_iter = mandelbrot::min_iteration_count;
constexpr int max_max_iter = mandelbrot::max_iteration_count;
constexpr int min_threads = 0; // 0 = auto
constexpr int max_threads = mandelbrot::max_thread_count;
constexpr int min_quality = 0;
constexpr int max_quality = 100;
constexpr double min_fps = 0.001;
constexpr double max_fps = 1000.0;
constexpr double min_duration = 0.001;
constexpr double max_duration = 86400.0;
constexpr double min_zoom = 1e-12;
constexpr double max_zoom = 1e12;
constexpr double min_center = -10.0;
constexpr double max_center = 10.0;

const std::unordered_map<std::string, int> subcommand_index = {
    {"-o", 0},
    {"--output", 0},
    {"-s", 1},
    {"--size", 1},
    {"--width", 2},
    {"--height", 3},
    {"--maxiter", 4},
    {"--fps", 5},
    {"--duration", 6},
    {"--kernel", 7},
    {"--threads", 8},
    {"--codec", 9},
    {"--quality", 10},
    {"--center", 11},
    {"--start-zoom", 12},
    {"--end-zoom", 13}
};

auto parse_int(const std::string& value, const std::string& arg, int min_value, int max_value) -> std::optional<int> {
    size_t consumed = 0;
    int parsed = 0;
    try {
        parsed = std::stoi(value, &consumed);
    } catch (const std::exception&) {
        std::cerr << "Invalid integer for " << arg << ": " << value << "\n";
        return std::nullopt;
    }
    if (consumed != value.size() || parsed < min_value || parsed > max_value) {
        std::cerr << "Value for " << arg << " must be an integer in [" << min_value << ", " << max_value << "], got: " << value << "\n";
        return std::nullopt;
    }
    return parsed;
}

auto parse_double(const std::string& value, const std::string& arg, double min_value, double max_value) -> std::optional<double> {
    size_t consumed = 0;
    double parsed = 0.0;
    try {
        parsed = std::stod(value, &consumed);
    } catch (const std::exception&) {
        std::cerr << "Invalid number for " << arg << ": " << value << "\n";
        return std::nullopt;
    }
    if (consumed != value.size() || !std::isfinite(parsed) || parsed < min_value || parsed > max_value) {
        std::cerr << "Value for " << arg << " must be a number in [" << min_value << ", " << max_value << "], got: " << value << "\n";
        return std::nullopt;
    }
    return parsed;
}

void print_version() {
    std::cout << "MandelbrotExplorer " << MANDELBROT_EXPLORER_VERSION << "\n";
}

void print_usage(const char* program) {
    // Read the defaults from RenderConfig itself so this text cannot drift.
    const RenderConfig defaults;
    const std::string default_threads = defaults.threads == 0 ? "auto" : std::to_string(defaults.threads);
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  -h, --help             Show this help message and exit\n"
              << "  -v, --version          Show the version and exit\n"
              << "  -o, --output <file>    Set output video file name (default: " << defaults.output_file << ")\n"
              << "  -s, --size <WxH>       Set video size (default: " << defaults.width << 'x' << defaults.height << ")\n"
              << "  --width <value>        Set video width (default: " << defaults.width << ")\n"
              << "  --height <value>       Set video height (default: " << defaults.height << ")\n"
              << "  --maxiter <value>      Set maximum iterations (default: " << defaults.max_iterations << ")\n"
              << "  --fps <value>          Set frames per second (default: " << defaults.fps << ")\n"
              << "  --duration <value>     Set duration in seconds (default: " << defaults.duration_seconds << ")\n"
              << "  --threads <value>      Set render thread count, 0 = auto (default: " << default_threads << ")\n"
              << "  --kernel <name>        Force a kernel: auto|scalar|avx2|avx512|neon (default: " << defaults.kernel << ")\n"
              << "  --codec <name>         Set video codec: mp4v|h264|h265|vp9|av1|mjpg (default: " << defaults.codec << ")\n"
              << "  --quality <value>      Set encoder quality 0-100 (default: " << defaults.quality << ", encoder default)\n"
              << "  --center <X,Y>         Set the animation center (default: " << defaults.animation.center_x << ',' << defaults.animation.center_y << ")\n"
              << "  --start-zoom <value>   Set the zoom of the first frame (default: " << defaults.animation.start_zoom << ")\n"
              << "  --end-zoom <value>     Set the zoom of the last frame (default: start-zoom * 1.05^280; equal to\n"
              << "                         start-zoom for a static video)\n";
}

} // namespace

auto parse_command_line(std::span<char* const> args, RenderConfig& config) -> std::optional<ExitStatus> {
    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        auto argument_entry = subcommand_index.find(arg);
        if (argument_entry != subcommand_index.end()) {
            int index = argument_entry->second;
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for argument: " << arg << "\n";
                return ExitStatus::InvalidArgument;
            }
            std::string value = args[++i];
            switch (index) {
                case 0:
                    if (value.empty()) {
                        std::cerr << "Output file name must not be empty.\n";
                        return ExitStatus::InvalidArgument;
                    }
                    config.output_file = value;
                    break;
                case 1:
                    {
                        size_t pos = value.find('x');
                        if (pos == std::string::npos || pos != value.rfind('x') || pos == 0 || pos + 1 == value.size()) {
                            std::cerr << "Invalid size format for " << arg << ": " << value << ". Please use WxH.\n";
                            return ExitStatus::InvalidArgument;
                        }
                        auto width = parse_int(value.substr(0, pos), arg + " (width)", min_dimension, max_dimension);
                        auto height = parse_int(value.substr(pos + 1), arg + " (height)", min_dimension, max_dimension);
                        if (!width || !height) {
                            return ExitStatus::InvalidArgument;
                        }
                        config.width = *width;
                        config.height = *height;
                    }
                    break;
                case 2: {
                    auto parsed = parse_int(value, arg, min_dimension, max_dimension);
                    if (!parsed) {
                        return ExitStatus::InvalidArgument;
                    }
                    config.width = *parsed;
                    break;
                }
                case 3: {
                    auto parsed = parse_int(value, arg, min_dimension, max_dimension);
                    if (!parsed) {
                        return ExitStatus::InvalidArgument;
                    }
                    config.height = *parsed;
                    break;
                }
                case 4: {
                    auto parsed = parse_int(value, arg, min_max_iter, max_max_iter);
                    if (!parsed) {
                        return ExitStatus::InvalidArgument;
                    }
                    config.max_iterations = *parsed;
                    break;
                }
                case 5: {
                    auto parsed = parse_double(value, arg, min_fps, max_fps);
                    if (!parsed) {
                        return ExitStatus::InvalidArgument;
                    }
                    config.fps = *parsed;
                    break;
                }
                case 6: {
                    auto parsed = parse_double(value, arg, min_duration, max_duration);
                    if (!parsed) {
                        return ExitStatus::InvalidArgument;
                    }
                    config.duration_seconds = *parsed;
                    break;
                }
                case 7:
                    if (value != "auto" && value != "scalar" && value != "avx2" &&
                        value != "avx512" && value != "neon") {
                        std::cerr << "Unknown kernel for " << arg << ": " << value
                                  << " (expected auto, scalar, avx2, avx512, or neon)\n";
                        return ExitStatus::InvalidArgument;
                    }
                    config.kernel = value;
                    break;
                case 8: {
                    auto parsed = parse_int(value, arg, min_threads, max_threads);
                    if (!parsed) {
                        return ExitStatus::InvalidArgument;
                    }
                    config.threads = *parsed;
                    break;
                }
                case 9:
                    if (value != "mp4v" && value != "h264" && value != "avc1" && value != "h265" &&
                        value != "hevc" && value != "vp9" && value != "av1" && value != "mjpg") {
                        std::cerr << "Unknown codec for " << arg << ": " << value
                                  << " (expected mp4v, h264, h265, vp9, av1, or mjpg)\n";
                        return ExitStatus::InvalidArgument;
                    }
                    config.codec = value;
                    break;
                case 10: {
                    auto parsed = parse_int(value, arg, min_quality, max_quality);
                    if (!parsed) {
                        return ExitStatus::InvalidArgument;
                    }
                    config.quality = *parsed;
                    break;
                }
                case 11: {
                    const size_t comma = value.find(',');
                    if (comma == std::string::npos || comma != value.rfind(',') ||
                        comma == 0 || comma + 1 == value.size()) {
                        std::cerr << "Invalid center format for " << arg << ": " << value << ". Please use X,Y.\n";
                        return ExitStatus::InvalidArgument;
                    }
                    auto center_x = parse_double(value.substr(0, comma), arg + " (x)", min_center, max_center);
                    auto center_y = parse_double(value.substr(comma + 1), arg + " (y)", min_center, max_center);
                    if (!center_x || !center_y) {
                        return ExitStatus::InvalidArgument;
                    }
                    config.animation.center_x = *center_x;
                    config.animation.center_y = *center_y;
                    break;
                }
                case 12: {
                    auto parsed = parse_double(value, arg, min_zoom, max_zoom);
                    if (!parsed) {
                        return ExitStatus::InvalidArgument;
                    }
                    config.animation.start_zoom = *parsed;
                    break;
                }
                case 13: {
                    auto parsed = parse_double(value, arg, min_zoom, max_zoom);
                    if (!parsed) {
                        return ExitStatus::InvalidArgument;
                    }
                    config.animation.end_zoom = *parsed;
                    break;
                }
                default:
                    std::cerr << "Unsolved argument: " << arg << "\nPlease report this bug.\n";
                    return ExitStatus::Bug;
            }
        }
        else if (arg == "-h" || arg == "--help") {
            print_usage(args.empty() ? "MandelbrotExplorer" : args[0]);
            return ExitStatus::Success;
        } else if (arg == "-v" || arg == "--version") {
            print_version();
            return ExitStatus::Success;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return ExitStatus::InvalidArgument;
        }
    }
    if (config.fps * config.duration_seconds < 1.0) {
        std::cerr << "FPS * duration must be at least 1 frame, got: " << config.fps * config.duration_seconds << "\n";
        return ExitStatus::InvalidArgument;
    }
    return std::nullopt;
}
