# 更新日志

本项目遵循[语义化版本](https://semver.org/lang/zh-CN/)；变更记录格式参考
[Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)。

## [Unreleased]

## [0.6.0] - 2026-09-30

首个包含 C ABI 共享库的版本：库、CMake 目标、头文件路径与环境变量均以项目名命名
（`mandelbrot_explorer` / `mandelbrot_explorer_video`），导出的 C 符号为 `mb_*`。
0.x 期间 C ABI 仍可能变动，破坏性变更会记录在本文件（`SOVERSION` 自 1.0 起跟随主版本号）。

### 新增

- 安装规则补全：`cmake --install` 现在也会安装 CLI 应用；Windows 上（vcpkg 预设）会把 OpenCV
  运行库连同其第三方依赖一起装到 `bin/`，安装目录可直接分发。
- 新增 `cpack` 分发包：Windows ZIP / 其他平台 TGZ，内含应用、库、头文件与 CMake 包配置。
- CMake 包支持：`cmake --install` 后可 `find_package(MandelbrotExplorer CONFIG)`，导入目标
  `MandelbrotExplorer::mandelbrot_explorer` 与 `MandelbrotExplorer::mandelbrot_explorer_video`
  （`SameMinorVersion` 版本校验；视频目标仅在 `MANDELBROT_EXPLORER_WITH_VIDEO=ON` 时提供）。
- C ABI 共享库 `mandelbrot_explorer`（`include/mandelbrot_explorer/mandelbrot_explorer.h`）：
  不透明 context 与一次性参数 API、BGR/RGB 颜色输出、int32 迭代数组输出、无外部依赖的
  BMP/PNM 写图、渲染取消；渲染核心不再依赖 OpenCV。
- 视频导出 API `mandelbrot_explorer_video`（`include/mandelbrot_explorer/video.h` 的
  `mb_write_video`），与 CLI 共用动画曲线（`src/ViewSequence.h`）与编码器名称映射
  （`src/VideoCodec.h`）。
- Python 绑定（`bindings/python`）：ctypes + numpy 零拷贝，支持颜色/迭代输出、写图与视频导出，
  无需编译。
- Python 打包：`bindings/python/pyproject.toml`、库 staging 脚本与 Linux/Windows/macOS 三平台
  wheel 构建工作流（Linux wheel 在 manylinux_2_28 容器内构建）；wheel 捆绑无 OpenCV 依赖的核心库，
  推送 `v*` tag 时自动创建 Release 并发布到 PyPI（Trusted Publishing），发布前校验 tag 与包/项目
  版本一致。
- CMake 选项 `MANDELBROT_EXPLORER_WITH_VIDEO`（默认 ON）：设为 OFF 时只构建无 OpenCV 依赖的
  核心库与 C ABI（Python wheel 即用此方式构建）。
- CLI 新选项：`--threads`、`--kernel`（新增 AVX-512 内核可选）、`--codec`、`--quality`、
  `-v/--version`。
- AVX-512 内核；NEON 内核与 AVX2 的结构和语义对齐。
- RelWithDebInfo 构建预设（优化 + 调试符号）。
- 文档：C ABI 使用教程（`docs/c-api.md`）、路线图（`ROADMAP.md`）、更新日志（本文件）。

### 变更

- 渲染与编码改为“帧缓冲池 + 写出线程”的生产-消费流水线，编码与渲染重叠执行。
- AVX2 内核重写：携带平方量、去掉累计掩码，避免寄存器溢出到栈。
- AVX-512 内核重写为双链结构。
- 头文件包含整理（补齐直接依赖、排序、架构守卫）。
- CI 权限最小化（`permissions: contents: read`）。
- CI：Linux/Windows/macOS 任务安装 numpy（`python_binding` 不再被跳过），新增 macOS、ASan/UBSan 与
  覆盖率门槛（行 >= 90%）任务；推送 `v*` tag 时自动创建 Release 并附带三平台 wheel。

### 修复

- 修复 AVX-512 内核在边界像素上与标量参考不一致的舍入问题。
- 修复 C 调用者传入非法枚举值时库内读取的未定义行为（C++ 侧枚举改为固定底层类型，
  sanitizer CI 发现）。
- 修复小尺寸视频渲染测试被误跳过的问题，并加强内核一致性测试（同时比较迭代数组）。
- 补齐错误路径与边界测试：行覆盖率 84% -> 92%（gcovr，Linux x64）。

## [0.5.0] - 2026-09-26

### 新增

- 首个正式发布：命令行视频生成器。
- 运行时分派的 SIMD 内核（标量 / AVX2 / NEON），多线程渲染（线程池 + 按行原子任务窃取），
  黑体辐射颜色映射，MP4 导出与平滑缩放动画。
- 参数校验与 `--help`；macOS（Homebrew）构建支持。

[Unreleased]: https://github.com/UnderOrangeTree/MandelbrotExplorer/compare/v0.6.0...HEAD
[0.6.0]: https://github.com/UnderOrangeTree/MandelbrotExplorer/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/UnderOrangeTree/MandelbrotExplorer/releases/tag/v0.5.0
