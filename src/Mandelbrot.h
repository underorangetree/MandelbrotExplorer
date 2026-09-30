#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>
#include "ThreadPool.h"
#include "MandelbrotKernel.h"
class Mandelbrot {
public:
    // Work-splitting strategy. Tiled is the default; PerRow reproduces the old
    // one-task-per-row scheme and exists for benchmarking.
    enum class Schedule : std::uint8_t { Tiled, PerRow };
    // Channel order of the rendered color frames. Bgr matches OpenCV/FFmpeg,
    // Rgb matches numpy/PIL conventions.
    enum class PixelOrder : std::uint8_t { Bgr, Rgb };
    // Outputs of one render. At least one of bgr/iterations must be set; the
    // strides are in bytes and must cover one row (storage_width() pixels).
    struct FrameTargets {
        uint8_t* bgr = nullptr;
        std::size_t bgr_stride = 0;
        std::int32_t* iterations = nullptr;
        std::size_t iterations_stride = 0;
    };
private:
    struct AlignedDelete {
        void operator()(double* ptr) const noexcept {
            operator delete(ptr, std::align_val_t(32));
        }
    };
    const int width;
    const int requested_width;
    const int height;
    const int max_iterations;
    double zoom = 1.0;
    double delta = 0.0;
    double offset_x = 0.0;
    double offset_y = 0.0;
    std::vector<std::array<uint8_t, 3>> color_map;
    std::unique_ptr<double, AlignedDelete> r_data;
    std::unique_ptr<double, AlignedDelete> i_data;
    ThreadPool thread_pool;
    std::vector<uint8_t> image; // height x width x 3, in pixel_order_
    uint8_t* color_output_ = nullptr;
    std::size_t color_step_ = 0;
    uint8_t* iteration_output_ = nullptr;
    std::size_t iteration_step_ = 0;
    std::atomic<bool> cancel_requested_{false};
    PixelOrder pixel_order_ = PixelOrder::Bgr;
    RowKernel kernel_ = nullptr;
    const char* kernel_name_ = "unknown";
    Schedule schedule_ = Schedule::Tiled;
public:
    // _threads = 0 selects std::thread::hardware_concurrency() render threads.
    Mandelbrot(int _width = 1920, int _height = 1080, int _maxIterations = 2000, int _threads = 0);
    Mandelbrot(const Mandelbrot&) = delete;
    auto operator=(const Mandelbrot&) -> Mandelbrot& = delete;
    Mandelbrot(Mandelbrot&&) = delete;
    auto operator=(Mandelbrot&&) -> Mandelbrot& = delete;
    void setView(double new_zoom, double new_offset_x, double new_offset_y);
    void set_schedule(Schedule schedule) { schedule_ = schedule; }
    [[nodiscard]] auto kernel_name() const -> const char*;
    [[nodiscard]] auto thread_count() const -> int;
    // Overrides the runtime-selected kernel. Returns false if the name is
    // unknown or the CPU does not support it.
    auto set_kernel(const char* name) -> bool;
    // Cancels a render in progress: the generate*() calls return after the rows
    // that are already running, leaving a partially rendered frame. The flag is
    // sticky until reset_cancel(); request_cancel() is safe to call from any
    // thread.
    void request_cancel() noexcept { cancel_requested_.store(true, std::memory_order_relaxed); }
    void reset_cancel() noexcept { cancel_requested_.store(false, std::memory_order_relaxed); }
    [[nodiscard]] auto is_cancelled() const noexcept -> bool {
        return cancel_requested_.load(std::memory_order_relaxed);
    }
private:
    void render_tile(int row_index, int x_begin, int x_end);
    void rebuild_color_map();
public:
    // Rebuilds the color map in the requested channel order; affects subsequent
    // renders only.
    void set_pixel_order(PixelOrder order);
    [[nodiscard]] auto pixel_order() const -> PixelOrder { return pixel_order_; }
    [[nodiscard]] auto frame_width() const -> int { return requested_width; }
    [[nodiscard]] auto frame_height() const -> int { return height; }
    // Padded width of the storage a frame must provide: the kernels render whole
    // 32-column blocks, so a smaller buffer would be written past its rows.
    [[nodiscard]] auto storage_width() const -> int { return width; }
    // Renders into the internal buffer and returns its first pixel. The buffer
    // holds `height` rows of `storage_width() * 3` bytes (BGR); only the first
    // `requested_width()` columns are meaningful.
    [[nodiscard]] auto generate() -> const uint8_t*;
    // First pixel of the last rendered frame (internal buffer).
    [[nodiscard]] auto frame_data() const -> const uint8_t* { return image.data(); }
    // Renders into the internal buffer, colors only.
    auto generate_into(uint8_t* buffer, std::size_t stride) -> void;
    // Renders into the requested outputs instead of the internal buffer, so
    // several frames can be in flight without copying and the iteration counts
    // can be captured. The caller must keep the buffers untouched until the
    // call returns.
    auto generate_frame(const FrameTargets& targets) -> void;
};
