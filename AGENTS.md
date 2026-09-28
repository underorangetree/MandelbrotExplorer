# AGENTS.md

Guidance for coding agents working in this repository.

## Project
MandelbrotExplorer generates Mandelbrot set zoom animations and exports MP4, written in C++20 with OpenCV.
A single executable with runtime-dispatched SIMD (scalar / AVX2 / NEON) and multithreaded rendering.

## Requirements
- C++20 compiler (GCC 13+ / Clang 17+ / MSVC 2019+; `<format>` needs a recent standard library)
- CMake >= 3.23
- OpenCV 4.x (`core`, `videoio`)
- Optional: vcpkg (the Windows presets use it to provide OpenCV)

## Build and test
Prefer the CMake presets in `CMakePresets.json`.

Windows (MSVC + vcpkg; `VCPKG_ROOT` must be set):
```
cmake --preset windows-msvc-vs2026
cmake --build --preset windows-msvc-vs2026-release
ctest --test-dir build/windows-msvc-vs2026 -C Release --output-on-failure
```

Linux (system OpenCV):
```
cmake --preset linux-gcc
cmake --build --preset linux-gcc-release
ctest --test-dir build/linux-gcc --output-on-failure
```

macOS (Homebrew):
```
cmake --preset macos-brew
cmake --build --preset macos-brew-release
```

Other presets: `windows-msvc-ninja` (hardcodes `cl`; run it from a Visual Studio developer command prompt), `windows-mingw-gcc`, `windows-mingw-clang`.
With the Visual Studio generator, `cmake --build` needs `--config Release` (or use the matching `*-release` build preset).

Before handing off, make sure the build succeeds and `ctest` passes. `cli_small_render` is skipped (exit code 3) when no usable video backend is available; that is expected.

## Layout
- `src/main.cpp` - entry point (`run()` plus top-level exception handling) and progress output.
- `src/Mandelbrot.{h,cpp}` - shared framework: allocation, validation, coordinate precomputation, color map, scheduling, cropping.
- `src/MandelbrotKernel.{h,cpp}` - `RowContext`, kernel declarations, runtime `select_kernel()`.
- `src/MandelbrotKernel_{scalar,avx2,avx512,neon}.cpp` - per-architecture inner-loop kernels.
- `src/ThreadPool.{h,cpp}` - thread pool.
- `src/CommandLine.{h,cpp}` - CLI parsing and `RenderConfig`.
- `src/MandelbrotLimits.h` - single source of truth for dimension/iteration bounds.
- `src/ExitStatus.h` - process exit codes.
- `tests/mandelbrot_tests.cpp` - unit and CLI tests; `tests/benchmark.cpp` - benchmark.

## Conventions and gotchas
- Comments are in English; identifiers use `lower_snake_case`; 4-space indentation; `#pragma once` in headers; keep the return-type style of the file you are editing.
- Architecture kernels: only `MandelbrotKernel_avx2.cpp` is compiled with `/arch:AVX2` (MSVC) or `-mavx2 -mfma` (GCC/Clang), and only `MandelbrotKernel_avx512.cpp` with `/arch:AVX512` or `-mavx512f`; every other translation unit stays on the baseline instruction set. The kernel files have no `#if` guards; CMake decides which ones are compiled.
- Runtime dispatch: when changing kernels or adding an architecture, update the CPU detection and `KernelSelection` in `MandelbrotKernel.cpp`.
- The scalar kernel is a bit-exact reference: `MandelbrotKernel_scalar.cpp` must mirror the SIMD kernels operation for operation (same `std::fma` usage, same association, same `< 4.0` escape test), otherwise `test_kernel_matches_scalar` fails.
- Width padding and cropping: the framework pads the width to a multiple of 32 (the widest kernel block is 16 columns, so 32 stays a safe multiple); `generate()` returns an ROI cropped to the requested width (possibly a non-continuous `cv::Mat`; the video writer handles `step`).
- Validation: both the CLI and the `Mandelbrot` constructor validate inputs; invalid constructor arguments throw `std::invalid_argument` rather than calling `std::exit`.
- Exit codes: `ExitStatus` is used both as the process exit code and as the CLI tests' `SKIP_RETURN_CODE` (3 when no video backend is available).
- Timing uses `steady_clock`; `elapsed`/`total_elapsed` can be 0, so guard divisions.
- Signals: `sigint_flag` is `volatile std::sig_atomic_t`; the handler must not perform I/O.

## CI
- `.github/workflows/cmake-linux.yml` - Linux x64 and ARM64 x gcc/clang (build + ctest).
- `.github/workflows/cmake-windows.yml` - Windows MSVC (OpenCV installed via choco).

## Docs
- `README.md` is user-facing; keep it in sync when CLI behavior changes.
- `LICENSE` - MIT.
