# MandelbrotExplorer 路线图

本文档列出通往 1.0 的发布计划，优先级大致为：发布准备 -> API 冻结 -> 功能收口 -> 分发与 CI。

## 当前状态（2026-09-30）

- `v0.6.0` 已发布：GitHub Release 附带三平台 wheel，PyPI（`mandelbrot-explorer`，Trusted Publishing）
  首次发布；CLI、C ABI 共享库、视频 API、Python 绑定、自定义视角、CMake 包与 CPack 均包含在 0.6.0 中。
- CI 覆盖 Linux x64/ARM64（gcc/clang，含 ASan/UBSan 与行覆盖率门槛 90%）、Windows MSVC 与
  macOS arm64；`v*` tag 自动构建 wheel、创建 Release 并发布 PyPI。
- 行覆盖率约 92%（gcovr，Linux x64；排除 CPU 相关的 AVX-512 内核文件）。

## 0.6.0 - 发布准备

- [x] 推送 `main` 上待发布的提交并确认 CI 通过
- [x] 版本号升到 0.6.0：`CMakeLists.txt` 的 `project(... VERSION ...)` 与
      `bindings/python/mandelbrot_explorer/__init__.py` 的 `__version__`（selftest 会校验二者一致，
      `mb_version()`、Python `version()`、CLI `--version` 跟随 CMake 版本）
- [x] 发布说明：`CHANGELOG.md` 的 0.6.0 条目（新库、性能改造，并注明 0.x 期间 C ABI 仍可能变动）
- [x] README 修正：`共享库 mandelbrot` -> `mandelbrot_explorer`；动画曲线位置改为 `src/ViewSequence.h`
- [x] CLI `-v/--version`

## 0.7 - 0.9 - 1.0 前置

### API 冻结

- [x] 在 README 写明三大公开面（CLI、C ABI、Python 包）与其 SemVer 承诺
- [x] C ABI 稳定性规则：`SOVERSION` 的升级条件、`struct_size` 加性演进、弃用流程
- [x] 文档化进程退出码（`ExitStatus`）与库状态码（`mb_status`）

### 功能收口

- [x] 自定义视角：中心点、起始缩放与结束缩放在 CLI、C ABI、Python 三个面同步暴露
      （缓动曲线形状暂不可调，见 README 已知限制）
- [ ] （可选）2x 帧复用性能优化（原型已验证 20-30% 加速且逐位一致）

### 分发与打包

- [x] CMake 包配置：`install(EXPORT)` + `MandelbrotExplorerConfig.cmake`，支持 `find_package`
- [x] 应用本体的 install 规则；Windows 运行期 OpenCV DLL 的处理说明（vcpkg `X_VCPKG_APPLOCAL_DEPS_INSTALL`）
- [x] Python 打包：`pyproject.toml` + staging 脚本 + Linux/Windows/macOS wheel 工作流
      （wheel 仅捆绑无依赖的核心库；Linux wheel 在 manylinux_2_28 容器内构建）
- [x] PyPI 发布基础设施：Trusted Publishing publish job + tag/版本一致性校验（`check_version.py`）
- [x] 在 PyPI 配置 Trusted Publisher（`mandelbrot-explorer`）并完成首发（0.6.0，2026-09-30）
- [x] （可选）CPack 压缩包（Windows ZIP / 其他平台 TGZ）
- [ ] （可选）vcpkg 端口

### CI 与质量

- [x] macOS CI job（arm64；Intel 尚未覆盖）
- [x] CI 安装 numpy，让 `python_binding` 真正执行而不是被 skip
- [x] ASan/UBSan 构建 job
- [x] tag 触发的发布自动化（`v*` 自动创建 Release 并附带三平台 wheel）
- [x] 覆盖率门槛（行 >= 90%，排除 CPU 相关的 AVX-512 内核文件）

### 文档

- [x] `CHANGELOG.md`
- [x] README 平台支持矩阵与已知限制
- [x] `docs/` 下的 C ABI 使用教程（`docs/c-api.md`）

## 1.0 完成标准

- 以上全部完成；
- 公开面（CLI/C ABI/Python）在 0.8 -> 1.0 之间无破坏性变更，即冻结满一个 minor 周期；
- 至少经历 0.6/0.7 两轮发布并收集到库与 Python 用户反馈。

## 非目标（1.0 前不考虑）

- GUI 界面
- 自定义分形公式（Mandelbrot 之外）
