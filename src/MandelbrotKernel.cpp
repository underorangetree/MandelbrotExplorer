#include "MandelbrotKernel.h"

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace {

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
auto cpu_supports_avx2_fma() -> bool {
    int regs[4] = {};
    __cpuid(regs, 0);
    if (regs[0] < 7) {
        return false;
    }
    __cpuidex(regs, 1, 0);
    const bool osxsave = (regs[2] & (1 << 27)) != 0;
    const bool avx = (regs[2] & (1 << 28)) != 0;
    const bool fma = (regs[2] & (1 << 12)) != 0;
    if (!osxsave || !avx || !fma) {
        return false;
    }
    const unsigned long long xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6ULL) != 0x6ULL) { // XMM + YMM state enabled by the OS
        return false;
    }
    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 5)) != 0; // EBX bit 5 = AVX2
}
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
bool cpu_supports_avx2_fma() {
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
}
#endif

} // namespace

auto select_kernel() -> KernelSelection {
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    if (cpu_supports_avx2_fma()) {
        return {.function=render_row_avx2, .name="avx2"};
    }
    return {.function=render_row_scalar, .name="scalar"};
#elif defined(__aarch64__)
    return {.function=render_row_neon, .name="neon"};
#else
    return {.function=render_row_scalar, .name="scalar"};
#endif
}
