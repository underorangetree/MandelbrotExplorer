# MandelbrotExplorer 路线图

本文档列出通往 1.0 的计划。1.0 的目标：**稳定的公开面（CLI / C ABI / Python / CMake 包）+ GUI +
GPU 加速（CPU 预览、GPU 实时渲染）**。GUI 与 GPU 都是可选组件：核心库保持零依赖，headless、Python
wheel 与现有 CI 不受影响。

## 当前状态（2026-09-30）

- `v0.6.0` 已发布：GitHub Release + PyPI（`mandelbrot-explorer`，Trusted Publishing）；CLI、C ABI
  共享库、视频 API、Python 绑定、自定义视角、CMake 包与 CPack 均已就绪。
- CI 全绿：Linux x64/ARM64（gcc/clang，含 ASan/UBSan 与行覆盖率门槛 90%）、Windows MSVC、
  macOS arm64；`v*` tag 自动构建 wheel、创建 Release 并发布 PyPI。
- 加性演进机制已就位：`struct_size`、`SOVERSION`、语义化版本政策（见 README「公开 API 与版本策略」）。

## 里程碑 A：GPU 后端（0.7）

目标：可选的 GPU 渲染后端，结果与 CPU 内核一致、可自动回退。

- [ ] 选型与依赖策略：以 **Vulkan compute** 为主（Windows/Linux/macOS，经 MoltenVK），CUDA 作为
      可选后端；运行时动态加载，无驱动/设备时回退 CPU，不新增硬依赖（保持核心零依赖与 wheel 可导入）；
      CMake 沿用现有模式加 `MANDELBROT_EXPLORER_WITH_GPU`（默认关）
- [ ] 精度：FP64 或 double-single 仿真 + 渐进细化；深缩放与 CPU 内核逐位/容差对比；设备能力不足时
      明确降级（实时预览可 FP32，导出走 FP64/扰动）
- [ ] 精度/深度对照表（文档）：各后端（FP32 / FP64 / double-single / 扰动）在什么缩放范围内保持准确，
      作为"实时预览深度上限"与降级策略的依据
- [ ] 接口（全部加性）：CLI `--backend cpu|gpu|auto`（`--kernel` 保留）、C ABI 扩展、Python 参数
- [ ] 渲染目标抽象：CPU/GPU/预览共用同一帧目标与调度接口（泛化 `FrameTargets`），避免多套渲染路径
- [ ] 多 GPU 设备选择；显存受限时的分块渲染
- [ ] 健壮性：device lost/驱动错误 → 回退 CPU 并提示一次
- [ ] 测试与基准：无 GPU 时跳过（沿用 exit 3 机制）；CI 用**软件 Vulkan**（lavapipe/SwiftShader）跑
      功能冒烟；GPU/CPU 一致性基准；记录性能基线防止回退
- [ ] （可选，延后）扰动理论 + 系列近似，支撑深缩放的实时预览与导出

## 里程碑 B：预览与实时渲染（0.7 - 0.8）

- [ ] 渐进细化渲染路径：低分辨率草稿 → 逐级细化，支持随时取消/更换视角（复用现有取消机制）
- [ ] CPU 预览：窗口内观看缩放动画，可暂停/继续/跳转
- [ ] GPU 实时渲染：交互帧率下的平移/缩放（受精度限制的深度上限；超出时提示或切 CPU 精细渲染）
- [ ] 交互：拖动平移、滚轮缩放、即时调整 maxiter/后端/配色
- [ ] 性能 HUD：帧率、单帧耗时、后端与设备信息
- [ ] GUI / CLI / 库共用同一份配置结构（单一来源，避免行为漂移）

## 里程碑 C：GUI（0.8 - 0.9）

- [ ] 框架选型：建议 **Dear ImGui + GLFW**（轻量、跨平台、可直接复用 GPU 上下文显示纹理）；
      备选 Qt（重）、OpenCV highgui（仅做最小原型）；CMake 加 `MANDELBROT_EXPLORER_WITH_GUI`（默认关）
- [ ] 功能：视角与动画参数（中心、起止缩放、时长、fps）、质量（maxiter）、后端选择、预览窗、
      渲染/导出视频（编码器/质量）、导出单帧图片、进度与取消、错误提示
- [ ] 配置持久化：上次设置、预设点（书签）；内置著名坐标预设库
- [ ] 应用细节：Windows DPI 感知、窗口图标、标题栏进度
- [ ] 打包：GUI 作为可选组件（不影响 core/wheel/headless）；CPack 含 GUI；Windows 随包 GUI 运行库；
      （可选）Windows 安装器（NSIS/WiX）
- [ ] 测试：GUI 逻辑与渲染解耦，CI 仅编译 + headless 冒烟，人工验收清单
- [ ] （可选）中英双语界面

## 其他 1.0 前可做（加性）

- [ ] 缓动/速率选项（`smoothstep` | `linear`，CLI/C ABI/Python 同步）——匀速动画与 2x 帧复用需要
- [ ] 统一配置：GUI/CLI 共用配置文件（导出/导入），便于复现某次渲染
- [ ] 配色系统：调色板选择/自定义、平滑迭代着色、内部色；GUI 与 CLI 同步
- [ ] （可选）2x 帧复用：仅当线性缓动且逐帧倍率恰为 2、宽高为偶数时启用；输出逐位一致
- [ ] （可选）vcpkg 端口
- [ ] 文档：GUI 使用说明（含截图/GIF 供 README 使用）、GPU 后端与精度/深度限制说明

## 1.0 判定

- 里程碑 A/B/C 完成，公开面（CLI/C ABI/Python/CMake 包）只做加性演进；
- 发布流程复用现有 tag → wheel/Release/PyPI 自动化；
- `SOVERSION` 跟随主版本（1.0.0 → `libmandelbrot_explorer.so.1`）。

## 非目标（1.0 前不考虑）

- 自定义分形公式（Mandelbrot 之外）
- 移动端 / Web 端
