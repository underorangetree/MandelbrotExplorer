#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Everything a kernel needs to render one row of the image. The framework
// prepares this (coordinates, output pointers, color map) and the per-architecture
// kernel only owns the escape-time inner loop.
struct RowContext {
    const double* r_data;                     // real parts, indexed by column
    double ci;                                // imaginary part of this row
    uint8_t* row_ptr;                         // start of the color output row (3 bytes per pixel)
    // x_begin and x_end must be a multiple of the kernel's block size: 16 for
    // AVX-512 (two 8-wide chains), 8 for AVX2, 4 for NEON, any value for the
    // scalar kernel. The framework pads the width to 32 and always renders
    // whole rows, so this holds today; keep it in mind before adding column tiling.
    int x_begin;                              // first column of the tile
    int x_end;                                // one past the last column of the tile
    int max_iterations;
    const std::array<uint8_t, 3>* color_map;  // length = max_iterations + 1
    // Optional iteration counts output (int32 per pixel, contiguous within the
    // row). At least one of row_ptr and iterations must be non-null; color_map
    // and row_ptr may be null when only the counts are requested.
    std::int32_t* iterations = nullptr;
    std::size_t iterations_step = 0;          // row stride in bytes, unused inside a row
};

using RowKernel = void (*)(const RowContext&);

struct KernelSelection {
    RowKernel function;
    const char* name;
};

void render_row_scalar(const RowContext& context);
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
void render_row_avx2(const RowContext& context);
void render_row_avx512(const RowContext& context);
#elif defined(__aarch64__)
void render_row_neon(const RowContext& context);
#endif

// Picks the best kernel available for the CPU executing this process.
[[nodiscard]] auto select_kernel() -> KernelSelection;

// Returns the named kernel if it exists in this build and the CPU supports it;
// otherwise returns a selection with a null function pointer.
[[nodiscard]] auto find_kernel(const char* name) -> KernelSelection;
