#include <cmath>
#include <cstdint>
#include "MandelbrotKernel.h"

// The arithmetic below intentionally mirrors the SIMD kernels operation for
// operation (same FMA usage, same association, same "< 4" escape test) so that
// the scalar kernel is a bit-exact reference for the vectorized ones.
void render_row_scalar(const RowContext& context) {
    const double ci = context.ci;
    const double y2 = ci * ci;
    const double y2_over_4 = y2 * 0.25;
    for (int col = context.x_begin; col < context.x_end; ++col) {
        uint8_t* const pixel_ptr = context.row_ptr + (col * 3);
        const double cr = context.r_data[col];
        const double xq = cr - 0.25;
        const double q = std::fma(xq, xq, y2);
        const double xp1 = cr + 1.0;
        const bool in_cardioid = q * (q + xq) <= y2_over_4;
        const bool in_bulb = std::fma(xp1, xp1, y2) <= 1.0 / 16.0;
        int iter = 0;
        if (in_cardioid || in_bulb) {
            iter = context.max_iterations;
        } else {
            double r = 0.0, i = 0.0;
            double ri = 0.0;
            double r2 = 0.0, i2 = 0.0;
            while (r2 + i2 < 4.0 && iter < context.max_iterations) {
                i = std::fma(2.0, ri, ci);
                r = r2 + (cr - i2);
                i2 = i * i;
                r2 = r * r;
                ri = r * i;
                ++iter;
            }
        }
        pixel_ptr[0] = context.color_map[iter][0];
        pixel_ptr[1] = context.color_map[iter][1];
        pixel_ptr[2] = context.color_map[iter][2];
    }
}
