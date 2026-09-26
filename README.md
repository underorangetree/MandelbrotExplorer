# MandelbrotExplorer

高性能的 Mandelbrot 集合视频制作器，使用 C++20 编写，结合 SIMD、多线程与 OpenCV 库，实现视频生成。

![Static Badge](https://img.shields.io/badge/C++-20-%2300599C?logo=cplusplus)
![GitHub repo size](https://img.shields.io/github/repo-size/UnderOrangeTree/MandelbrotExplorer)
![License](https://img.shields.io/badge/license-MIT-blue)

~~首先，这个仓库是个史山，请不要在这个上花费太多的时间~~

## 特性
- 🚀 运行时分派的 SIMD 加速 – 启动时按运行 CPU 自动选择标量 / AVX2（x86）/ NEON（ARM）内核，每次迭代同时处理多个像素点。
- 🧵 多线程渲染 – 线程池配合按行的原子任务窃取，动态均衡各行的负载。
- 🎨 彩色可视化 – 采用近似 <6000K 黑体辐射颜色映射。
- 🎥 视频导出 – 生成 MP4 视频，支持平滑缩放动画。

## 环境要求
- 支持 C++20 的编译器
- CMake 3.23 或更高版本
- OpenCV 4.x（需包含开发文件）
- （可选）vcpkg，用于在 Windows 上获取 OpenCV

## 编译

### 使用 CMake Presets（推荐）
Windows（MSVC + vcpkg）：先让 `VCPKG_ROOT` 指向 vcpkg，再在仓库根目录执行：

```bash
cmake --preset windows-msvc-vs2026        # 或 windows-msvc-vs2022
cmake --build --preset windows-msvc-vs2026-release
```
`windows-msvc-ninja` 用法相同，但它硬编码了 `cl`，需要在 **Visual Studio 开发者命令行**（已运行 `vcvars64.bat`）里执行；`windows-msvc-vs2022`/`windows-msvc-vs2026`（VS 生成器）不需要。

`CMakeUserPresets.json`（已被 gitignore）可以定义一个带 `VCPKG_ROOT` 的本地 `default` preset。

Linux 使用系统 OpenCV（无需 vcpkg）：

```bash
sudo apt install libopencv-dev
cmake --preset linux-gcc
cmake --build --preset linux-gcc-release
```

macOS 使用 Homebrew（Apple Silicon 与 Intel 均适用）：

```bash
brew install opencv ninja cmake
cmake --preset macos-brew
cmake --build --preset macos-brew-release
```

Windows 上用 MSYS2 / MinGW（在对应的 MSYS2 shell 里执行）：

```bash
# UCRT64 示例；包名前缀按环境替换（MINGW64: mingw-w64-x86_64-，CLANG64: mingw-w64-clang-x86_64-）
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
                   mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-opencv
cmake --preset windows-mingw-gcc          # CLANG64 用 windows-mingw-clang
cmake --build --preset windows-mingw-gcc-release
```
需要 GCC 13+ 或 Clang 17+（`<format>`）。

### 手动配置
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Linux 上可先 `sudo apt install libopencv-dev`；Windows 上手动使用 vcpkg 时加上
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`。

## 运行

```bash
./build/MandelbrotExplorer        # Windows: build\Release\MandelbrotExplorer.exe
```

默认行为：
- 渲染分辨率 1920×1080、时长 10 秒、60 FPS 的缩放动画。
- 保存为当前目录下的 `mandelbrot.mp4`。
- 打印所选 SIMD 内核、线程数，以及渲染进度与性能统计。

### 命令行参数
```
-h, --help            显示帮助并退出
-o, --output <file>   输出文件名（默认 mandelbrot.mp4）
-s, --size <WxH>      视频尺寸 WxH（默认 1920x1080）
--width <value>       视频宽度（默认 1920）
--height <value>      视频高度（默认 1080）
--maxiter <value>     单点最大迭代次数（默认 2000）
--fps <value>         帧率（默认 60）
--duration <value>    时长，单位秒（默认 10）
```

示例：
```bash
MandelbrotExplorer -s 1280x720 --maxiter 1000 --fps 30 --duration 5 -o zoom.mp4
```

参数会被校验：宽/高 `[1, 32768]`、`--maxiter` `[1, 1000000]`、`--fps` `[0.001, 1000]`、
`--duration` `[0.001, 86400]`，且 `FPS × 时长 ≥ 1` 帧；非法输入会打印错误并退出。

缩放动画曲线与目标中心点仍硬编码在 `main.cpp` 中。

## 测试
```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
包含单元测试（已知点渲染、裁剪、线程池、CLI 解析、调度一致性）与 CLI 冒烟测试；
需要视频后端才能录制视频的用例，在没有可用编码器时会被标记为 skipped。

另附带一个基准程序：
```bash
mandelbrot_benchmark             # 固定视角，对比按行与原子任务窃取调度
mandelbrot_benchmark anim ...    # 按应用同样的缩放曲线对比总耗时
```

## 架构简介
- `Mandelbrot.cpp` – 共享框架：生命周期、坐标预计算、颜色表、调度与裁剪。
- `MandelbrotKernel_{scalar,avx2,neon}.cpp` – 每架构的内层迭代内核。
- `MandelbrotKernel.cpp` – 运行时 CPU 探测与内核选择（`__cpuid`/`_xgetbv` 或 `__builtin_cpu_supports`）。
- `ThreadPool.cpp` / `CommandLine.cpp` – 线程池与命令行解析。

## 许可证

本项目基于 [MIT 许可证](LICENSE) 发布。

第三方依赖：[OpenCV](https://opencv.org/)（Apache-2.0 许可证），发布二进制时请一并保留其许可声明。
