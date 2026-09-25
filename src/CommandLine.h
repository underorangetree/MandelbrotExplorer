#pragma once
#include <optional>
#include <span>
#include <string>
#include "ExitStatus.h"

struct RenderConfig {
    int width = 1920;
    int height = 1080;
    int max_iterations = 2000;
    double fps = 60.0;
    double duration_seconds = 10.0;
    std::string output_file = "mandelbrot.mp4";
};

// Parses command-line arguments into config.
// Returns std::nullopt when parsing succeeded and the program should continue.
// Returns an ExitStatus when the caller should terminate with that status:
// ExitStatus::Success after --help, otherwise an error code.
auto parse_command_line(std::span<char* const> args, RenderConfig& config) -> std::optional<ExitStatus>;
