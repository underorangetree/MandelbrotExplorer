#include <array>
#include <cstdint>
#include <arm_neon.h>
#include "MandelbrotKernel.h"

namespace {
constexpr int simd_size = 2;
constexpr int chain_count = 2;
constexpr int iteration_check_period = 2;
}

void render_row_neon(const RowContext& context) {
    const int x_begin = context.x_begin;
    const int x_end = context.x_end;
    const int max_iterations = context.max_iterations;
    const std::array<uint8_t, 3>* const color_map = context.color_map;
    uint8_t* const row_ptr = context.row_ptr;
    const double* const r_data = context.r_data;
    alignas(16) int64_t iterations_a[simd_size];
    alignas(16) int64_t iterations_b[simd_size];
    const float64x2_t y_vec = vdupq_n_f64(context.ci);
    const float64x2_t zeros = vdupq_n_f64(0.0);
    const float64x2_t sixteenth = vdupq_n_f64(1.0 / 16.0);
    const float64x2_t quarter = vdupq_n_f64(0.25);
    const float64x2_t ones = vdupq_n_f64(1.0);
    const float64x2_t twos = vdupq_n_f64(2.0);
    const float64x2_t fours = vdupq_n_f64(4.0);
    const float64x2_t y2 = vmulq_f64(y_vec, y_vec);
    const float64x2_t y2_over_4 = vmulq_f64(y2, quarter);
    const uint64x2_t max_iter_vec = vdupq_n_u64(static_cast<uint64_t>(max_iterations));
    for (int x = x_begin; x < x_end; x += chain_count * simd_size) {
        const float64x2_t x_vec_a = vld1q_f64(r_data + x);
        const float64x2_t x_vec_b = vld1q_f64(r_data + x + simd_size);
        // Skip points inside the main cardioid or the period-2 bulb up front.
        uint64x2_t skip_a;
        uint64x2_t skip_b;
        uint64x2_t continue_a;
        uint64x2_t continue_b;
        {
            const float64x2_t xq = vsubq_f64(x_vec_a, quarter);
            const float64x2_t q = vmlaq_f64(y2, xq, xq);
            const uint64x2_t in_cardioid = vcleq_f64(vmulq_f64(q, vaddq_f64(q, xq)), y2_over_4);
            const float64x2_t xp1 = vaddq_f64(x_vec_a, ones);
            const uint64x2_t in_bulb = vcleq_f64(vmlaq_f64(y2, xp1, xp1), sixteenth);
            skip_a = vorrq_u64(in_cardioid, in_bulb);
            continue_a = vceqq_u64(skip_a, vdupq_n_u64(0));
        }
        {
            const float64x2_t xq = vsubq_f64(x_vec_b, quarter);
            const float64x2_t q = vmlaq_f64(y2, xq, xq);
            const uint64x2_t in_cardioid = vcleq_f64(vmulq_f64(q, vaddq_f64(q, xq)), y2_over_4);
            const float64x2_t xp1 = vaddq_f64(x_vec_b, ones);
            const uint64x2_t in_bulb = vcleq_f64(vmlaq_f64(y2, xp1, xp1), sixteenth);
            skip_b = vorrq_u64(in_cardioid, in_bulb);
            continue_b = vceqq_u64(skip_b, vdupq_n_u64(0));
        }
        uint64x2_t iteration_vec_a = vbicq_u64(max_iter_vec, continue_a);
        uint64x2_t iteration_vec_b = vbicq_u64(max_iter_vec, continue_b);
        // Only the squares are carried between iterations; r and i are derived
        // from them. The escape test is recomputed every iteration instead of
        // accumulating a mask: for |c| <= 2 a lane that escaped (|z| >= 2) can
        // never fall back inside, so its counter stops on its own. Skipped
        // lanes start with all-ones bits (NaN) in their real square so they
        // never pass the comparison and keep the max_iterations value
        // preloaded in their counter.
        float64x2_t r2_a = vreinterpretq_f64_u64(skip_a);
        float64x2_t i2_a = zeros;
        float64x2_t ri_a = zeros;
        float64x2_t r2_b = vreinterpretq_f64_u64(skip_b);
        float64x2_t i2_b = zeros;
        float64x2_t ri_b = zeros;
        for (int iter = 0; iter < max_iterations; ++iter) {
            const uint64x2_t alive_a = vcltq_f64(vaddq_f64(r2_a, i2_a), fours);
            const uint64x2_t alive_b = vcltq_f64(vaddq_f64(r2_b, i2_b), fours);
            if ((iter % iteration_check_period) == 0 &&
                vaddvq_u64(alive_a) == 0 && vaddvq_u64(alive_b) == 0)
                break;
            iteration_vec_a = vsubq_u64(iteration_vec_a, alive_a);
            iteration_vec_b = vsubq_u64(iteration_vec_b, alive_b);
            // vec a
            const float64x2_t i_a = vfmaq_f64(y_vec, ri_a, twos);
            const float64x2_t r_a = vaddq_f64(r2_a, vsubq_f64(x_vec_a, i2_a));
            i2_a = vmulq_f64(i_a, i_a);
            r2_a = vmulq_f64(r_a, r_a);
            ri_a = vmulq_f64(r_a, i_a);
            // vec b
            const float64x2_t i_b = vfmaq_f64(y_vec, ri_b, twos);
            const float64x2_t r_b = vaddq_f64(r2_b, vsubq_f64(x_vec_b, i2_b));
            i2_b = vmulq_f64(i_b, i_b);
            r2_b = vmulq_f64(r_b, r_b);
            ri_b = vmulq_f64(r_b, i_b);
        }
        vst1q_s64(iterations_a, vreinterpretq_s64_u64(iteration_vec_a));
        vst1q_s64(iterations_b, vreinterpretq_s64_u64(iteration_vec_b));
        for (int dx = 0; dx < simd_size; ++dx) {
            uint8_t* const pixel_ptr = row_ptr + (x + dx) * 3;
            int64_t iter = iterations_a[dx];
            pixel_ptr[0] = color_map[iter][0];
            pixel_ptr[1] = color_map[iter][1];
            pixel_ptr[2] = color_map[iter][2];
        }
        for (int dx = 0; dx < simd_size; ++dx) {
            uint8_t* const pixel_ptr = row_ptr + (x + simd_size + dx) * 3;
            int64_t iter = iterations_b[dx];
            pixel_ptr[0] = color_map[iter][0];
            pixel_ptr[1] = color_map[iter][1];
            pixel_ptr[2] = color_map[iter][2];
        }
    }
}
