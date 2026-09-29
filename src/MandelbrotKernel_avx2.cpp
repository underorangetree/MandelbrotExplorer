#include <array>
#include <cstdint>
#include <immintrin.h>
#include "MandelbrotKernel.h"

namespace {
constexpr int simd_size = 4;
// Dead lanes cannot change again, so an exit check every other iteration is enough.
constexpr int iteration_check_period = 2;
}

void render_row_avx2(const RowContext& context) {
    const int x_begin = context.x_begin;
    const int x_end = context.x_end;
    const int max_iterations = context.max_iterations;
    const std::array<uint8_t, 3>* const color_map = context.color_map;
    uint8_t* const row_ptr = context.row_ptr;
    const double* const r_data = context.r_data;
    alignas(32) int64_t iterations_a[simd_size];
    alignas(32) int64_t iterations_b[simd_size];
    const __m256d y_vec = _mm256_set1_pd(context.ci);
    const __m256d zeros = _mm256_setzero_pd();
    const __m256d sixteenth = _mm256_set1_pd(1.0 / 16.0);
    const __m256d quarter = _mm256_set1_pd(0.25);
    const __m256d ones = _mm256_set1_pd(1.0);
    const __m256d twos = _mm256_set1_pd(2.0);
    const __m256d fours = _mm256_set1_pd(4.0);
    const __m256d y2 = _mm256_mul_pd(y_vec, y_vec);
    const __m256d y2_over_4 = _mm256_mul_pd(y2, quarter);
    const __m256i max_iter_vec = _mm256_set1_epi64x(static_cast<int64_t>(max_iterations));
    for (int x = x_begin; x < x_end; x += 2 * simd_size) {
        const __m256d x_vec_a = _mm256_load_pd(r_data + x);
        const __m256d x_vec_b = _mm256_load_pd(r_data + x + simd_size);
        // Before iterating, skip points inside the main cardioid or the period-2
        // bulb, since they never escape.
        // Cardioid: with q = (x - 0.25)^2 + y^2, points satisfying
        // q * (q + (x - 0.25)) <= 0.25 * y^2 are inside.
        // Period-2 bulb: points satisfying (x + 1)^2 + y^2 <= 1/16 are inside.
        // vec a
        __m256d xq = _mm256_sub_pd(x_vec_a, quarter);
        __m256d q = _mm256_fmadd_pd(xq, xq, y2);
        __m256d in_cardioid_mask = _mm256_cmp_pd(_mm256_mul_pd(q, _mm256_add_pd(q, xq)), y2_over_4, _CMP_LE_OQ);
        __m256d xp1 = _mm256_add_pd(x_vec_a, ones);
        __m256d in_bulb_mask = _mm256_cmp_pd(_mm256_fmadd_pd(xp1, xp1, y2), sixteenth, _CMP_LE_OQ);
        const __m256d skip_a = _mm256_or_pd(in_cardioid_mask, in_bulb_mask);
        const __m256d continue_mask_a = _mm256_cmp_pd(skip_a, zeros, _CMP_EQ_OQ);
        // vec b
        xq = _mm256_sub_pd(x_vec_b, quarter);
        q = _mm256_fmadd_pd(xq, xq, y2);
        in_cardioid_mask = _mm256_cmp_pd(_mm256_mul_pd(q, _mm256_add_pd(q, xq)), y2_over_4, _CMP_LE_OQ);
        xp1 = _mm256_add_pd(x_vec_b, ones);
        in_bulb_mask = _mm256_cmp_pd(_mm256_fmadd_pd(xp1, xp1, y2), sixteenth, _CMP_LE_OQ);
        const __m256d skip_b = _mm256_or_pd(in_cardioid_mask, in_bulb_mask);
        const __m256d continue_mask_b = _mm256_cmp_pd(skip_b, zeros, _CMP_EQ_OQ);
        __m256i iteration_vec_a = _mm256_andnot_si256(_mm256_castpd_si256(continue_mask_a), max_iter_vec);
        __m256i iteration_vec_b = _mm256_andnot_si256(_mm256_castpd_si256(continue_mask_b), max_iter_vec);
        // Only the squares are carried between iterations; r and i are derived
        // from them, which keeps the loop out of registers spilling to the
        // stack. The escape test is recomputed every iteration instead of
        // accumulating a mask: for |c| <= 2 a lane that escaped (|z| >= 2) can
        // never fall back inside, so its counter stops on its own. Skipped
        // lanes start as all-ones bits (NaN) so they never pass the comparison
        // and keep the max_iterations value preloaded in their counter.
        __m256d r2_a = skip_a;
        __m256d i2_a = zeros;
        __m256d ri_a = zeros;
        __m256d r2_b = skip_b;
        __m256d i2_b = zeros;
        __m256d ri_b = zeros;
        for (int iter = 0; iter < max_iterations; ++iter) {
            const __m256d alive_a = _mm256_cmp_pd(_mm256_add_pd(r2_a, i2_a), fours, _CMP_LT_OQ);
            const __m256d alive_b = _mm256_cmp_pd(_mm256_add_pd(r2_b, i2_b), fours, _CMP_LT_OQ);
            if ((iter % iteration_check_period) == 0 &&
                _mm256_movemask_pd(alive_a) == 0 && _mm256_movemask_pd(alive_b) == 0)
                break;
            iteration_vec_a = _mm256_sub_epi64(iteration_vec_a, _mm256_castpd_si256(alive_a));
            iteration_vec_b = _mm256_sub_epi64(iteration_vec_b, _mm256_castpd_si256(alive_b));
            const __m256d i_a = _mm256_fmadd_pd(twos, ri_a, y_vec);
            const __m256d r_a = _mm256_add_pd(r2_a, _mm256_sub_pd(x_vec_a, i2_a));
            const __m256d i_b = _mm256_fmadd_pd(twos, ri_b, y_vec);
            const __m256d r_b = _mm256_add_pd(r2_b, _mm256_sub_pd(x_vec_b, i2_b));
            i2_a = _mm256_mul_pd(i_a, i_a);
            r2_a = _mm256_mul_pd(r_a, r_a);
            ri_a = _mm256_mul_pd(r_a, i_a);
            i2_b = _mm256_mul_pd(i_b, i_b);
            r2_b = _mm256_mul_pd(r_b, r_b);
            ri_b = _mm256_mul_pd(r_b, i_b);
        }
        _mm256_store_si256(reinterpret_cast<__m256i*>(iterations_a), iteration_vec_a);
        _mm256_store_si256(reinterpret_cast<__m256i*>(iterations_b), iteration_vec_b);
        for (int dx = 0; dx < simd_size; ++dx) {
            uint8_t* const pixel_ptr = row_ptr + (x + dx) * 3;
            uint64_t iter = iterations_a[dx];
            pixel_ptr[0] = color_map[iter][0];
            pixel_ptr[1] = color_map[iter][1];
            pixel_ptr[2] = color_map[iter][2];
        }
        for (int dx = 0; dx < simd_size; ++dx) {
            uint8_t* const pixel_ptr = row_ptr + (x + simd_size + dx) * 3;
            uint64_t iter = iterations_b[dx];
            pixel_ptr[0] = color_map[iter][0];
            pixel_ptr[1] = color_map[iter][1];
            pixel_ptr[2] = color_map[iter][2];
        }
    }
}
