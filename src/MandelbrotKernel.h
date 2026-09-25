#pragma once
#include <array>
#include <cstdint>

// Everything a kernel needs to render one row of the image. The framework
// prepares this (coordinates, output pointer, color map) and the per-architecture
// kernel only owns the escape-time inner loop.
struct RowContext {
    const double* r_data;                     // real parts, indexed by column
    double ci;                                // imaginary part of this row
    uint8_t* row_ptr;                         // start of the output row (3 bytes per pixel)
    int x_begin;                              // first column of the tile (multiple of 8)
    int x_end;                                // one past the last column of the tile
    int max_iterations;
    const std::array<uint8_t, 3>* color_map;  // length = max_iterations + 1
};

using RowKernel = void (*)(const RowContext&);

struct KernelSelection {
    RowKernel function;
    const char* name;
};

void render_row_scalar(const RowContext& context);
void render_row_avx2(const RowContext& context);
void render_row_neon(const RowContext& context);

// Picks the best kernel available for the CPU executing this process.
[[nodiscard]] auto select_kernel() -> KernelSelection;
