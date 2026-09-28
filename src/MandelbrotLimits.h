#pragma once

// Shared numeric limits for image dimensions and iteration counts, used by both
// the command-line parser and the Mandelbrot class so the two cannot drift.
namespace mandelbrot {

inline constexpr int min_dimension = 1;
inline constexpr int max_dimension = 32768;
inline constexpr int min_iteration_count = 1;
inline constexpr int max_iteration_count = 1000000;
inline constexpr int max_thread_count = 1024; // 0 threads means auto (hardware_concurrency)

} // namespace mandelbrot
