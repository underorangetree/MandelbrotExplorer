# Python 绑定

对 C ABI 共享库（`mandelbrot_explorer.dll` / `libmandelbrot_explorer.so` / `libmandelbrot_explorer.dylib`）的轻量
封装：ctypes + numpy，**无需编译步骤**，颜色与迭代数组都是零拷贝（库直接写入 numpy 缓冲）。

## 安装

### 使用 wheel

CI（`.github/workflows/python-wheels.yml`）为 Linux / Windows / macOS 构建 wheel，wheel 内已捆绑对应平台的
核心库；Linux wheel 在 manylinux_2_28 容器内构建，兼容较旧的 glibc。推送 `v*` tag 时会自动创建 Release
并发布到 PyPI（Trusted Publishing）：

```bash
pip install mandelbrot-explorer                 # 从 PyPI 安装（0.6.0 起）
# 或从 Release 下载对应平台的 wheel：
pip install mandelbrot_explorer-*-py3-none-*.whl
```

wheel 不包含 OpenCV 版的视频库；`write_video` 需要另行构建 `mandelbrot_explorer_video` 并用环境变量
`MANDELBROT_EXPLORER_VIDEO_LIBRARY` 指向它（见第 4、5 节）。

### 从源码安装

先按仓库 README 构建共享库，再把库拷进包目录并安装：

```bash
cmake --build build --config Release
python bindings/python/stage_libraries.py --build-dir build   # 需要视频库时加 --video
pip install bindings/python
```

也可以完全不安装：在仓库内直接运行（把 `bindings/python` 加入 `sys.path` 或从该目录运行），
绑定会自动在 `build/` 下查找库（见下节）。

## 使用

先构建共享库（`mandelbrot_explorer` 目标），然后：

```python
import mandelbrot_explorer as me

with me.Mandelbrot(1280, 720, max_iterations=1000) as m:   # threads 0 = auto
    m.set_view(4.0, -0.743643887037158704752191506114774,
               0.131825904205311970493132056385139)
    colors, iterations = m.render(colors=True, iterations=True)    # (h, w, 3) uint8 / (h, w) int32
    m.set_pixel_order("rgb")
    rgb = m.render()
    m.write_image("frame.bmp")                                      # bmp / pnm，无外部依赖
```

一次调用（不需要 context）：

```python
colors = me.render_frame(1920, 1080, max_iterations=2000, zoom=2.0, center=(-0.5, 0.0))
me.render_image("frame.ppm", 1920, 1080, max_iterations=2000, format="pnm")
```

自定义缓冲（零拷贝，可复用）：

```python
import numpy as np
colors = np.empty((720, m.storage_width, 3), dtype=np.uint8)   # storage_width 是按 32 对齐的宽度
iterations = np.empty((720, m.storage_width), dtype=np.int32)
m.render_into(colors, iterations)                              # 使用 colors[:, :m.width] 得到有效区域
```

视频导出（需要 OpenCV 版的 `mandelbrot_explorer_video` 库；找不到库抛 `FileNotFoundError`，
没有可用编码器抛 `me.VideoError`）：

```python
me.write_video("zoom.mp4", 1920, 1080, max_iterations=2000, threads=0,
                        fps=60, duration=10, codec="h264", quality=-1)
# 自定义视角：center 为中心点，start_zoom/end_zoom 为两端缩放（相等即静态视角）
me.write_video("custom.mp4", 1280, 720, fps=30, duration=5,
                        center=(-0.7436, 0.1318), start_zoom=1.0, end_zoom=1000.0)
```

不传 `center`/`start_zoom`/`end_zoom` 时使用与 CLI 相同的默认动画；每帧倍率由帧数推算，
最后一帧精确到达 `end_zoom`。

## 库的查找顺序

`m.set_...` 之前无需设置路径，绑定按以下顺序查找：

1. `mandelbrot.load_library("path")` 或构造函数 / 函数参数 `library=...`
2. 环境变量 `MANDELBROT_EXPLORER_LIBRARY`（视频库为 `MANDELBROT_EXPLORER_VIDEO_LIBRARY`）
3. 包目录（与 `__init__.py` 同级）
4. 仓库构建目录（`build/*/Release/mandelbrot_explorer.dll`、`build/*/libmandelbrot_explorer.so` 等）
5. 系统搜索路径（`mandelbrot_explorer.dll` / `libmandelbrot_explorer.so` / `libmandelbrot_explorer.dylib`）

## 自测与示例

```bash
python3 bindings/python/selftest.py --library build/libmandelbrot_explorer.so
python3 bindings/python/examples/render_image.py --size 1280x720 --iterations out.bmp
```

`selftest.py` 需要 numpy；缺少 numpy 时以退出码 3 跳过（与 ctest 的 `SKIP_RETURN_CODE` 一致），
`cmake` 找到 Python 解释器时会自动把它注册为 `python_binding` 测试。
