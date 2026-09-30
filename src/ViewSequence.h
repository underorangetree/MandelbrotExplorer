#pragma once
#include <algorithm>
#include <cmath>

// The zoom animation shared by the application, the benchmark and the video
// API: a smoothstep ease (the cubic Bezier with control points (1/3, 0) and
// (2/3, 1), i.e. cubic-bezier(0.33, 0, 0.67, 1)) over an exponential zoom
// between a start and an end zoom around a fixed center. Keep this the single
// source of truth so the exported videos cannot drift apart.
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

// One zoom animation. end_zoom <= 0 keeps the historical depth factor
// (start_zoom * rate^exponent), so the defaults reproduce the original
// animation; start_zoom == end_zoom yields a static view.
struct ZoomAnimation {
    double center_x = animation_center_x;
    double center_y = animation_center_y;
    double start_zoom = animation_start_zoom;
    double end_zoom = 0.0;
};

inline auto default_end_zoom(double start_zoom) -> double {
    return start_zoom * std::pow(animation_rate, animation_exponent);
}

// Zoom of frame `frame` (0-based) in a sequence of `frame_count` frames. The
// per-frame ratio (the base of the exponential) is derived from the frame
// count, so the last frame reaches exactly end_zoom; a single-frame sequence
// stays at start_zoom. frame_count must be at least 1.
inline auto animation_zoom(double frame, double frame_count, const ZoomAnimation& animation = {}) -> double {
    const double start_zoom = animation.start_zoom;
    if (frame <= 0.0 || frame_count <= 1.0) {
        return start_zoom;
    }
    const double end_zoom = animation.end_zoom > 0.0 ? animation.end_zoom : default_end_zoom(start_zoom);
    const double progress = std::min(frame / (frame_count - 1.0), 1.0);
    const double eased = smoothstep(0.0, 1.0, progress, 1.0);
    return start_zoom * std::pow(end_zoom / start_zoom, eased);
}

} // namespace mandelbrot
