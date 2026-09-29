#include <array>
#include <cstdint>
#include <immintrin.h>
#include "MandelbrotKernel.h"

namespace {
constexpr int simd_size = 8;
constexpr int chain_count = 2; // 2 independent chains hide the z = z^2 + c latency
}

void render_row_avx512(const RowContext& context) {
    const int x_begin = context.x_begin;
    const int x_end = context.x_end;
    const int max_iterations = context.max_iterations;
    const std::array<uint8_t, 3>* const color_map = context.color_map;
    uint8_t* const row_ptr = context.row_ptr;
    const double* const r_data = context.r_data;
    alignas(64) int64_t iterations[chain_count][simd_size];
    const __m512d y_vec = _mm512_set1_pd(context.ci);
    const __m512d zeros = _mm512_setzero_pd();
    const __m512d sixteenth = _mm512_set1_pd(1.0 / 16.0);
    const __m512d quarter = _mm512_set1_pd(0.25);
    const __m512d ones = _mm512_set1_pd(1.0);
    const __m512d twos = _mm512_set1_pd(2.0);
    const __m512d fours = _mm512_set1_pd(4.0);
    const __m512d y2 = _mm512_mul_pd(y_vec, y_vec);
    const __m512d y2_over_4 = _mm512_mul_pd(y2, quarter);
    const __m512i max_iter_vec = _mm512_set1_epi64(max_iterations);
    const __m512i minus_one = _mm512_set1_epi64(-1);
    for (int x = x_begin; x < x_end; x += chain_count * simd_size) {
        const __m512d x_vec_0 = _mm512_loadu_pd(r_data + x);
        const __m512d x_vec_1 = _mm512_loadu_pd(r_data + x + simd_size);
        __m512d r_0 = zeros;
        __m512d i_0 = zeros;
        __m512d r_1 = zeros;
        __m512d i_1 = zeros;
        __m512i iteration_vec_0;
        __m512i iteration_vec_1;
        __mmask8 continue_mask_0;
        __mmask8 continue_mask_1;
        // Before iterating, skip points inside the main cardioid or the
        // period-2 bulb, since they never escape.
        // Cardioid: with q = (x - 0.25)^2 + y^2, points satisfying
        // q * (q + (x - 0.25)) <= 0.25 * y^2 are inside.
        // Period-2 bulb: points satisfying (x + 1)^2 + y^2 <= 1/16 are inside.
        {
            const __m512d xq = _mm512_sub_pd(x_vec_0, quarter);
            const __m512d q = _mm512_fmadd_pd(xq, xq, y2);
            const __mmask8 in_cardioid = _mm512_cmp_pd_mask(_mm512_mul_pd(q, _mm512_add_pd(q, xq)), y2_over_4, _CMP_LE_OQ);
            const __m512d xp1 = _mm512_add_pd(x_vec_0, ones);
            const __mmask8 in_bulb = _mm512_cmp_pd_mask(_mm512_fmadd_pd(xp1, xp1, y2), sixteenth, _CMP_LE_OQ);
            continue_mask_0 = static_cast<__mmask8>(~(in_cardioid | in_bulb));
            iteration_vec_0 = _mm512_maskz_mov_epi64(static_cast<__mmask8>(~continue_mask_0), max_iter_vec);
        }
        {
            const __m512d xq = _mm512_sub_pd(x_vec_1, quarter);
            const __m512d q = _mm512_fmadd_pd(xq, xq, y2);
            const __mmask8 in_cardioid = _mm512_cmp_pd_mask(_mm512_mul_pd(q, _mm512_add_pd(q, xq)), y2_over_4, _CMP_LE_OQ);
            const __m512d xp1 = _mm512_add_pd(x_vec_1, ones);
            const __mmask8 in_bulb = _mm512_cmp_pd_mask(_mm512_fmadd_pd(xp1, xp1, y2), sixteenth, _CMP_LE_OQ);
            continue_mask_1 = static_cast<__mmask8>(~(in_cardioid | in_bulb));
            iteration_vec_1 = _mm512_maskz_mov_epi64(static_cast<__mmask8>(~continue_mask_1), max_iter_vec);
        }
        for (int iter = 0; iter < max_iterations; ++iter) {
            // One set of squares per chain, reused by the escape test and the
            // step, so only r and i are carried between iterations.
            {
                const __m512d r2 = _mm512_mul_pd(r_0, r_0);
                const __m512d i2 = _mm512_mul_pd(i_0, i_0);
                const __m512d ri = _mm512_mul_pd(r_0, i_0);
                continue_mask_0 = _mm512_mask_cmp_pd_mask(continue_mask_0, _mm512_add_pd(r2, i2), fours, _CMP_LT_OQ);
                iteration_vec_0 = _mm512_mask_sub_epi64(iteration_vec_0, continue_mask_0, iteration_vec_0, minus_one);
                i_0 = _mm512_fmadd_pd(twos, ri, y_vec);
                r_0 = _mm512_add_pd(r2, _mm512_sub_pd(x_vec_0, i2));
            }
            {
                const __m512d r2 = _mm512_mul_pd(r_1, r_1);
                const __m512d i2 = _mm512_mul_pd(i_1, i_1);
                const __m512d ri = _mm512_mul_pd(r_1, i_1);
                continue_mask_1 = _mm512_mask_cmp_pd_mask(continue_mask_1, _mm512_add_pd(r2, i2), fours, _CMP_LT_OQ);
                iteration_vec_1 = _mm512_mask_sub_epi64(iteration_vec_1, continue_mask_1, iteration_vec_1, minus_one);
                i_1 = _mm512_fmadd_pd(twos, ri, y_vec);
                r_1 = _mm512_add_pd(r2, _mm512_sub_pd(x_vec_1, i2));
            }
            if ((iter & 3) == 0 && !(continue_mask_0 | continue_mask_1)) {
                break;
            }
        }
        _mm512_store_si512(reinterpret_cast<__m512i*>(iterations[0]), iteration_vec_0);
        _mm512_store_si512(reinterpret_cast<__m512i*>(iterations[1]), iteration_vec_1);
        for (int c = 0; c < chain_count; ++c) {
            for (int d = 0; d < simd_size; ++d) {
                uint8_t* const pixel_ptr = row_ptr + (x + c * simd_size + d) * 3;
                const uint64_t iter = static_cast<uint64_t>(iterations[c][d]);
                pixel_ptr[0] = color_map[iter][0];
                pixel_ptr[1] = color_map[iter][1];
                pixel_ptr[2] = color_map[iter][2];
            }
        }
    }
}
