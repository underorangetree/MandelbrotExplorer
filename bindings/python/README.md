# Python 绑定

对 C ABI 共享库（`mandelbrot.dll` / `libmandelbrot.so` / `libmandelbrot.dylib`）的轻量
封装：ctypes + numpy，**无需编译步骤**，颜色与迭代数组都是零拷贝（库直接写入 numpy 缓冲）。

## 使用

先构建共享库（`mandelbrot` 目标），然后：

```python
import mandelbrot

with mandelbrot.Mandelbrot(1280, 720, max_iterations=1000) as m:   # threads 0 = auto
    m.set_view(4.0, -0.743643887037158704752191506114774,
               0.131825904205311970493132056385139)
    colors, iterations = m.render(colors=True, iterations=True)    # (h, w, 3) uint8 / (h, w) int32
    m.set_pixel_order("rgb")
    rgb = m.render()
    m.write_image("frame.bmp")                                      # bmp / pnm，无外部依赖
```

一次调用（不需要 context）：

```python
colors = mandelbrot.render_frame(1920, 1080, max_iterations=2000, zoom=2.0, center=(-0.5, 0.0))
mandelbrot.render_image("frame.ppm", 1920, 1080, max_iterations=2000, format="pnm")
```

自定义缓冲（零拷贝，可复用）：

```python
import numpy as np
colors = np.empty((720, m.storage_width, 3), dtype=np.uint8)   # storage_width 是按 32 对齐的宽度
iterations = np.empty((720, m.storage_width), dtype=np.int32)
m.render_into(colors, iterations)                              # 使用 colors[:, :m.width] 得到有效区域
```

视频导出（需要 OpenCV 版的 `mandelbrot_video` 库；找不到库抛 `FileNotFoundError`，
没有可用编码器抛 `mandelbrot.VideoError`）：

```python
mandelbrot.write_video("zoom.mp4", 1920, 1080, max_iterations=2000, threads=0,
                        fps=60, duration=10, codec="h264", quality=-1)
```

## 库的查找顺序

`m.set_...` 之前无需设置路径，绑定按以下顺序查找：

1. `mandelbrot.load_library("path")` 或构造函数 / 函数参数 `library=...`
2. 环境变量 `MANDELBROT_LIBRARY`（视频库为 `MANDELBROT_VIDEO_LIBRARY`）
3. 包目录（与 `__init__.py` 同级）
4. 仓库构建目录（`build/*/Release/mandelbrot.dll`、`build/*/libmandelbrot.so` 等）
5. 系统搜索路径（`mandelbrot.dll` / `libmandelbrot.so` / `libmandelbrot.dylib`）

## 自测与示例

```bash
python3 bindings/python/selftest.py --library build/libmandelbrot.so
python3 bindings/python/examples/render_image.py --size 1280x720 --iterations out.bmp
```

`selftest.py` 需要 numpy；缺少 numpy 时以退出码 3 跳过（与 ctest 的 `SKIP_RETURN_CODE` 一致），
`cmake` 找到 Python 解释器时会自动把它注册为 `python_binding` 测试。
