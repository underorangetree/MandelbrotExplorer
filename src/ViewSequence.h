#pragma once
#include <cmath>

// The zoom animation shared by the application, the benchmark and the video
// API: a smoothstep-eased exponential zoom around a fixed center. Keep this the
// single source of truth so the exported videos cannot drift apart.
namespace mandelbrot {

inline constexpr double animation_center_x = -0.743643887037158704752191506114774;
inline constexpr double animation_center_y = 0.131825904205311970493132056385139;
inline constexpr double animation_start_zoom = 0.8;
inline constexpr double animation_rate = 1.05;
inline constexpr double animation_exponent = 280.0;

inline auto smoothstep(double low, double high, double t, double t0) -> double {
    const double x = t / t0;
    const double weight = x * x * (3.0 - (2.0 * x));
    return low + ((high - low) * weight);
}

// Zoom of frame `frame` (0-based) in a sequence of `frame_count` frames;
// frame_count must be at least 1.
inline auto animation_zoom(double frame, double frame_count) -> double {
    const double exponent = smoothstep(0.0, animation_exponent, frame, frame_count);
    return animation_start_zoom * std::pow(animation_rate, exponent);
}

} // namespace mandelbrot
