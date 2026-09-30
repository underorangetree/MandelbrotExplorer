#pragma once
#include <optional>
#include <span>
#include <string>
#include "ExitStatus.h"
#include "ViewSequence.h"

struct RenderConfig {
    int width = 1920;
    int height = 1080;
    int max_iterations = 2000;
    double fps = 60.0;
    double duration_seconds = 10.0;
    std::string output_file = "mandelbrot.mp4";
    std::string kernel = "auto"; // auto, scalar, avx2, avx512, neon
    int threads = 0;             // 0 = auto (hardware_concurrency)
    std::string codec = "mp4v";  // mp4v, h264, h265, vp9, av1, mjpg
    int quality = -1;            // 0..100, -1 = encoder default
    mandelbrot::ZoomAnimation animation; // defaults reproduce the original animation
};

// Parses command-line arguments into config.
// Returns std::nullopt when parsing succeeded and the program should continue.
// Returns an ExitStatus when the caller should terminate with that status:
// ExitStatus::Success after --help, otherwise an error code.
auto parse_command_line(std::span<char* const> args, RenderConfig& config) -> std::optional<ExitStatus>;
