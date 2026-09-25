#include <cstdint>
#include <arm_neon.h>
#include "MandelbrotKernel.h"

namespace {
constexpr int simd_size = 2;
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
    const uint64x2_t zeros_int = vdupq_n_u64(0);
    const float64x2_t zeros = vdupq_n_f64(0.0);
    const float64x2_t sixteenth = vdupq_n_f64(1.0 / 16.0);
    const float64x2_t quarter = vdupq_n_f64(0.25);
    const float64x2_t ones = vdupq_n_f64(1.0);
    const float64x2_t twos = vdupq_n_f64(2.0);
    const float64x2_t fours = vdupq_n_f64(4.0);
    const float64x2_t y2 = vmulq_f64(y_vec, y_vec);
    const float64x2_t y2_over_4 = vmulq_f64(y2, quarter);
    const uint64x2_t max_iter_vec = vdupq_n_u64(static_cast<uint64_t>(max_iterations));
    for (int x = x_begin; x < x_end; x += 2 * simd_size) {
        const float64x2_t x_vec_a = vld1q_f64(r_data + x);
        float64x2_t r_a = zeros;
        float64x2_t i_a = zeros;
        float64x2_t ri_a = zeros;
        float64x2_t r2_a = zeros;
        float64x2_t i2_a = zeros;
        uint64x2_t iteration_vec_a = zeros_int;
        float64x2_t mod_squared_vec_a = zeros;
        const float64x2_t x_vec_b = vld1q_f64(r_data + x + simd_size);
        float64x2_t r_b = zeros;
        float64x2_t i_b = zeros;
        float64x2_t ri_b = zeros;
        float64x2_t r2_b = zeros;
        float64x2_t i2_b = zeros;
        uint64x2_t iteration_vec_b = zeros_int;
        float64x2_t mod_squared_vec_b = zeros;
        // Skip points inside the main cardioid or the period-2 bulb up front.
        float64x2_t xq = vsubq_f64(x_vec_a, quarter);
        float64x2_t q = vmlaq_f64(y2, xq, xq);
        uint64x2_t in_cardioid_mask = vcleq_f64(vmulq_f64(q, vaddq_f64(q, xq)), y2_over_4);
        float64x2_t xp1 = vaddq_f64(x_vec_a, ones);
        uint64x2_t in_bulb_mask = vcleq_f64(vmlaq_f64(y2, xp1, xp1), sixteenth);
        uint64x2_t continue_mask_a = vceqq_u64(vorrq_u64(in_cardioid_mask, in_bulb_mask), vdupq_n_u64(0));
        xq = vsubq_f64(x_vec_b, quarter);
        q = vmlaq_f64(y2, xq, xq);
        in_cardioid_mask = vcleq_f64(vmulq_f64(q, vaddq_f64(q, xq)), y2_over_4);
        xp1 = vaddq_f64(x_vec_b, ones);
        in_bulb_mask = vcleq_f64(vmlaq_f64(y2, xp1, xp1), sixteenth);
        uint64x2_t continue_mask_b = vceqq_u64(vorrq_u64(in_cardioid_mask, in_bulb_mask), vdupq_n_u64(0));
        iteration_vec_a = vbicq_u64(max_iter_vec, continue_mask_a);
        iteration_vec_b = vbicq_u64(max_iter_vec, continue_mask_b);
        for (int iter = 0; iter < max_iterations; ++iter) {
            continue_mask_a = vandq_u64(continue_mask_a, vcltq_f64(mod_squared_vec_a, fours));
            continue_mask_b = vandq_u64(continue_mask_b, vcltq_f64(mod_squared_vec_b, fours));
            if (vaddvq_u64(continue_mask_a) == 0 && vaddvq_u64(continue_mask_b) == 0)
                break;
            // vec a
            iteration_vec_a = vsubq_u64(iteration_vec_a, continue_mask_a);
            i_a = vfmaq_f64(y_vec, ri_a, twos);
            r_a = vaddq_f64(r2_a, vsubq_f64(x_vec_a, i2_a));
            i2_a = vmulq_f64(i_a, i_a);
            r2_a = vmulq_f64(r_a, r_a);
            ri_a = vmulq_f64(r_a, i_a);
            mod_squared_vec_a = vaddq_f64(r2_a, i2_a);
            // vec b
            iteration_vec_b = vsubq_u64(iteration_vec_b, continue_mask_b);
            i_b = vfmaq_f64(y_vec, ri_b, twos);
            r_b = vaddq_f64(r2_b, vsubq_f64(x_vec_b, i2_b));
            i2_b = vmulq_f64(i_b, i_b);
            r2_b = vmulq_f64(r_b, r_b);
            ri_b = vmulq_f64(r_b, i_b);
            mod_squared_vec_b = vaddq_f64(r2_b, i2_b);
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
