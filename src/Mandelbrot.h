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
    std::vector<uint8_t> image; // height x width x 3, BGR
    uint8_t* output_data_ = nullptr;
    std::size_t output_step_ = 0;
    std::atomic<bool> cancel_requested_{false};
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
public:
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
    // Renders into a caller-owned BGR8 buffer instead of the internal one, so
    // several frames can be in flight without copying. `stride` is in bytes and
    // must be at least `storage_width() * 3`; the caller must keep the buffer
    // untouched until the call returns.
    auto generate_into(uint8_t* buffer, std::size_t stride) -> void;
};
