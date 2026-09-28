#include <cstdint>
#include <immintrin.h>
#include "MandelbrotKernel.h"

namespace {
constexpr int simd_size = 8;
constexpr int chain_count = 4; // 4 independent chains hide the z = z^2 + c latency
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
        __m512d x_vec[chain_count];
        __m512d r[chain_count];
        __m512d i[chain_count];
        __mmask8 continue_mask[chain_count];
        __m512i iteration_vec[chain_count];
        for (int c = 0; c < chain_count; ++c) {
            x_vec[c] = _mm512_loadu_pd(r_data + x + c * simd_size);
            r[c] = zeros;
            i[c] = zeros;
            // Before iterating, skip points inside the main cardioid or the
            // period-2 bulb, since they never escape.
            const __m512d xq = _mm512_sub_pd(x_vec[c], quarter);
            const __m512d q = _mm512_fmadd_pd(xq, xq, y2);
            const __mmask8 in_cardioid = _mm512_cmp_pd_mask(_mm512_mul_pd(q, _mm512_add_pd(q, xq)), y2_over_4, _CMP_LE_OQ);
            const __m512d xp1 = _mm512_add_pd(x_vec[c], ones);
            const __mmask8 in_bulb = _mm512_cmp_pd_mask(_mm512_fmadd_pd(xp1, xp1, y2), sixteenth, _CMP_LE_OQ);
            continue_mask[c] = static_cast<__mmask8>(~(in_cardioid | in_bulb));
            iteration_vec[c] = _mm512_maskz_mov_epi64(static_cast<__mmask8>(~continue_mask[c]), max_iter_vec);
        }
        for (int iter = 0; iter < max_iterations; ++iter) {
            // Test the four masks without moving them to general-purpose registers.
            if (_mm512_kortestz(static_cast<__mmask16>(continue_mask[0] | continue_mask[1]),
                                static_cast<__mmask16>(continue_mask[2] | continue_mask[3])) != 0) {
                break;
            }
            for (int c = 0; c < chain_count; ++c) {
                // One set of squares per chain, reused by the escape test and the
                // step, so only r and i are carried between iterations.
                const __m512d r2 = _mm512_mul_pd(r[c], r[c]);
                const __m512d i2 = _mm512_mul_pd(i[c], i[c]);
                const __m512d ri = _mm512_mul_pd(r[c], i[c]);
                continue_mask[c] = _mm512_mask_cmp_pd_mask(continue_mask[c], _mm512_add_pd(r2, i2), fours, _CMP_LT_OQ);
                iteration_vec[c] = _mm512_mask_sub_epi64(iteration_vec[c], continue_mask[c], iteration_vec[c], minus_one);
                i[c] = _mm512_fmadd_pd(twos, ri, y_vec);
                r[c] = _mm512_add_pd(r2, _mm512_sub_pd(x_vec[c], i2));
            }
        }
        for (int c = 0; c < chain_count; ++c) {
            _mm512_store_si512(reinterpret_cast<__m512i*>(iterations[c]), iteration_vec[c]);
        }
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