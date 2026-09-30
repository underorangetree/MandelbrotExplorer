# C ABI 使用教程

MandelbrotExplorer 提供两个 C ABI 共享库，任何支持 FFI 的语言都可以调用：

| 库 | 头文件 | 职责 | 运行期依赖 |
| --- | --- | --- | --- |
| `mandelbrot_explorer` | `include/mandelbrot_explorer/mandelbrot_explorer.h` | 渲染（颜色/迭代）、写图、取消 | 无（仅系统线程库） |
| `mandelbrot_explorer_video` | `include/mandelbrot_explorer/video.h` | 缩放动画导出视频 | OpenCV（core + videoio） |

导出的 C 符号以 `mb_` 开头，错误通过 `mb_status` 返回；C++ 异常不会穿过库边界。

## 构建

正常构建（见 README）会生成：

- Linux/macOS：`build/libmandelbrot_explorer.so`、`build/libmandelbrot_explorer_video.so`
  （macOS 为 `.dylib`，带 `SOVERSION` 符号链接）；
- Windows：`build/<config>/mandelbrot_explorer.dll` 与导入库 `.lib`（`<config>` 如 `Release`）。

头文件在 `include/mandelbrot_explorer/`。也可以 `cmake --install <build> --prefix <dir> --config Release`
安装：`bin/`（两个共享库与 CLI 应用）、`include/`、`lib/cmake/MandelbrotExplorer/`（CMake 包）。
Windows 上使用 vcpkg 预设时，OpenCV 运行库及其第三方依赖会一并安装到 `bin/`。

Windows 上核心库没有额外运行期依赖；`mandelbrot_explorer_video` 需要 OpenCV 的 DLL 位于
`PATH` 或 exe 同目录。

### 在 CMake 项目中使用

安装后可以直接被 CMake 项目消费：

```cmake
find_package(MandelbrotExplorer CONFIG REQUIRED)
target_link_libraries(app PRIVATE MandelbrotExplorer::mandelbrot_explorer)        # 无依赖的核心库
target_link_libraries(app PRIVATE MandelbrotExplorer::mandelbrot_explorer_video)  # 可选：OpenCV 版视频库
```

用 `-DCMAKE_PREFIX_PATH=<安装前缀>`（或 `MandelbrotExplorer_DIR=<前缀>/lib/cmake/MandelbrotExplorer`）
指向安装位置；以 `MANDELBROT_EXPLORER_WITH_VIDEO=OFF` 构建的安装只提供核心目标。CMake 会按
`SameMinorVersion` 校验版本（0.x 期间次版本变化视为可能不兼容）。Windows 上运行期还需要把
`bin/` 下的 DLL 放到可执行文件旁或 `PATH` 中。

## 核心概念

### 上下文与线程

`mb_create(width, height, max_iterations, threads)` 创建自带工作线程池的渲染上下文；
`threads = 0` 表示自动（按硬件并发数）。**一个 context 不要跨线程并发调用**，建议每个线程
使用独立 context；`mb_cancel` 是唯一例外，可从任意线程调用。

### 尺寸与填充

- `mb_width()` / `mb_height()`：请求的（有效）尺寸；
- `mb_storage_width()`：实际行宽，按 32 列对齐（SIMD 内核按整块渲染）。

每行缓冲区至少要有 `mb_storage_width() * 3` 字节，但只有前 `mb_width()` 列有意义。

### 频道顺序

默认 `MB_BGR`（OpenCV/FFmpeg 顺序）；`mb_set_pixel_order(ctx, MB_RGB)` 切换到 RGB
（numpy/PIL 顺序）。切换只影响后续渲染。

### 迭代数组

`mb_render_outputs()` 可以同时输出颜色和 int32 的每像素逃逸代数：集合内部为
`max_iterations`。两个 stride 的单位都是**字节**。

### 错误处理

所有可能失败的调用返回 `mb_status`：

| 状态码 | 含义 |
| --- | --- |
| `MB_OK` | 成功 |
| `MB_INVALID_ARGUMENT` | 参数或上下文非法 |
| `MB_INTERNAL_ERROR` | 内存或文件等内部错误 |
| `MB_CANCELLED` | 渲染被取消，帧为部分渲染 |
| `MB_VIDEO_ERROR` | 编码器打不开（仅视频 API） |

`mb_last_error()` 返回**当前线程**最近一次失败的消息；`mb_create()` 失败返回 `NULL`
（用 `mb_last_error()` 取原因），其余函数对 `NULL` context 分别返回
`MB_INVALID_ARGUMENT` / `0` / `NULL`。

### 取消

`mb_cancel(ctx)` 请求取消进行中的渲染：调用在已启动的行渲染完后返回 `MB_CANCELLED`，
帧是部分渲染的。标志是粘性的，**下一次渲染开始时会自动清除**。

## 最小示例（C）

```c
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "mandelbrot_explorer/mandelbrot_explorer.h"

int main(void) {
    mb_context* ctx = mb_create(1280, 720, 1000, 0); /* threads 0 = auto */
    if (ctx == NULL) {
        fprintf(stderr, "%s\n", mb_last_error());
        return 1;
    }

    /* 深焦点，与 CLI/视频动画的目标中心一致 */
    mb_set_view(ctx, 4.0, -0.743643887037158704752191506114774,
                         0.131825904205311970493132056385139);

    const size_t color_stride = (size_t)mb_storage_width(ctx) * 3;
    const size_t iter_stride  = (size_t)mb_storage_width(ctx) * sizeof(int32_t);
    uint8_t* colors     = malloc((size_t)mb_height(ctx) * color_stride);
    int32_t* iterations = malloc((size_t)mb_height(ctx) * iter_stride);
    if (colors == NULL || iterations == NULL) {
        return 1;
    }

    const mb_status status = mb_render_outputs(ctx, colors, color_stride, iterations, iter_stride);
    if (status != MB_OK) {
        fprintf(stderr, "%s\n", mb_last_error());
        return 1;
    }

    /* 迭代数组按 storage_width 索引（不是 mb_width） */
    printf("center iteration count: %d\n",
           iterations[(size_t)(mb_height(ctx) / 2) * mb_storage_width(ctx) + mb_width(ctx) / 2]);

    free(iterations);
    free(colors);
    mb_destroy(ctx);
    return 0;
}
```

编译运行（Linux）：

```bash
gcc example.c -Iinclude -Lbuild -lmandelbrot_explorer -Wl,-rpath,$PWD/build -o example
./example
```

Windows（MSVC 开发者命令行）：

```bat
cl example.c /Iinclude /link build\windows-msvc-vs2026\Release\mandelbrot_explorer.lib
```

把 `mandelbrot_explorer.dll` 放到 exe 同目录或加入 `PATH` 后运行。

## 一次性参数 API

不管理 context 时，用 `mb_render_params`（必须填 `struct_size`）+ `mb_render_frame` /
`mb_render_image` 渲染单帧。注意：一次性 API 不返回 `mb_storage_width()`，行宽需自行按
32 列对齐推算：

```c
#include <stdint.h>
#include <stdlib.h>
#include "mandelbrot_explorer/mandelbrot_explorer.h"

int main(void) {
    mb_render_params params = {0};
    params.struct_size = sizeof(params);
    params.width = 1280;
    params.height = 720;
    params.max_iterations = 1000;
    params.zoom = 2.0;
    params.center_x = -0.5;
    params.center_y = 0.0;
    params.kernel = "scalar";   /* NULL 或 "auto" = 运行时选择 */
    params.pixel_order = MB_BGR;

    /* 行宽的填充规则与 mb_storage_width() 相同：对齐到 32 的倍数 */
    const size_t storage_width = ((size_t)params.width + 31u) & ~(size_t)31u;
    uint8_t* colors = (uint8_t*)malloc((size_t)params.height * storage_width * 3);
    if (colors == NULL) {
        return 1;
    }

    const mb_status status = mb_render_frame(&params, colors, storage_width * 3, NULL, 0);
    /* mb_render_image(&params, "frame.bmp", MB_IMAGE_BMP) 可直接写图，无需自己分配 */
    free(colors);
    return status == MB_OK ? 0 : 1;
}
```

`width` / `height` / `max_iterations` / `threads` / `pixel_order` 非法都会返回
`MB_INVALID_ARGUMENT`；`iterations` 输出与颜色输出至少要设置一个。

## 导出动画视频

```c
#include <stdio.h>
#include "mandelbrot_explorer/video.h"

int main(void) {
    mb_video_params vp = {0};
    vp.struct_size = sizeof(vp);
    vp.width = 1280;
    vp.height = 720;
    vp.max_iterations = 1000;
    vp.threads = 0;              /* 0 = auto */
    vp.fps = 60;
    vp.duration_seconds = 10;
    vp.kernel = "auto";          /* NULL = auto */
    vp.pixel_order = MB_BGR;
    vp.codec = "h264";           /* NULL 或空串 = "mp4v" */
    vp.quality = -1;             /* < 0 = 编码器默认 */

    const mb_status status = mb_write_video(&vp, "zoom.mp4");
    if (status == MB_VIDEO_ERROR) {
        fprintf(stderr, "no usable video backend: %s\n", mb_video_last_error());
        return 3; /* 可据此把用例标记为跳过 */
    }
    if (status != MB_OK) {
        fprintf(stderr, "failed: %s\n", mb_video_last_error());
        return 1;
    }
    return 0;
}
```

要点：

- 动画曲线与 CLI 完全一致（共用 `src/ViewSequence.h`），编码器名称集合也一致
  （`mp4v|h264|avc1|h265|hevc|vp9|av1|mjpg`，见 `src/VideoCodec.h`）；
- `fps` 与 `duration_seconds` 必须有限且为正，且 `fps * duration >= 1` 帧；
- `MB_VIDEO_ERROR` 只表示"没有可用编码器"，与参数错误（`MB_INVALID_ARGUMENT`）区分开。

## 从其他语言调用

- **Python**：本仓库自带绑定（`bindings/python`，ctypes + numpy 零拷贝）：
  `import mandelbrot_explorer as me`，用法见该目录的 README。
- **C# P/Invoke**（节选，`mandelbrot_explorer.dll` 需在搜索路径中）：

  ```csharp
  using System;
  using System.Runtime.InteropServices;

  internal static class Native {
      [DllImport("mandelbrot_explorer", CallingConvention = CallingConvention.Cdecl)]
      internal static extern IntPtr mb_create(int width, int height, int maxIterations, int threads);

      [DllImport("mandelbrot_explorer", CallingConvention = CallingConvention.Cdecl)]
      internal static extern void mb_destroy(IntPtr ctx);

      [DllImport("mandelbrot_explorer", CallingConvention = CallingConvention.Cdecl)]
      internal static extern int mb_set_view(IntPtr ctx, double zoom, double centerX, double centerY);

      [DllImport("mandelbrot_explorer", CallingConvention = CallingConvention.Cdecl)]
      internal static extern int mb_storage_width(IntPtr ctx);

      [DllImport("mandelbrot_explorer", CallingConvention = CallingConvention.Cdecl)]
      internal static extern int mb_height(IntPtr ctx);

      [DllImport("mandelbrot_explorer", CallingConvention = CallingConvention.Cdecl)]
      internal static extern int mb_render_into(IntPtr ctx, byte[] bgr, nuint stride);
  }
  ```

- **Rust / Go / Java（Panama）等**：按头文件声明绑定即可；注意 `size_t`/指针宽度、C 调用约定，
  以及 `mb_*` 的导出符号名。

## ABI 兼容性

- 句柄（`mb_context`）不透明；导出的 `mb_*` 符号及其语义是公开约定，语义变更按破坏性变更处理；
- `mb_render_params` / `mb_video_params` 均以 `struct_size` 开头，按以下规则演进：
  - 调用者始终填写自己头文件版本的 `sizeof`；
  - 后续版本只在**结构体末尾追加字段**，库只读取 `struct_size` 覆盖到的字段，未覆盖的新字段使用
    默认值，因此旧调用者仍可工作；
  - 当前所有字段都是初始字段，`struct_size` 小于 `sizeof(...)` 会返回
    `MB_INVALID_ARGUMENT`，尽早暴露头文件与库不匹配；
- 未知枚举值会被拒绝（返回 `MB_INVALID_ARGUMENT`），不会触发未定义行为；
- 弃用流程：先在头文件注释与 `CHANGELOG.md` 标注，至少保留一个次版本号后再移除；
- 版本与 `SOVERSION`：0.x 期间次版本号递增可能破坏 ABI（`SOVERSION` 保持 `0`），请固定使用具体
  版本；1.0 起 `SOVERSION` 跟随主版本号，破坏性变更只随主版本发生。

## 常见陷阱

- **stride 的单位是字节**（颜色行与迭代行都是），不是像素；
- 只读取前 `mb_width()` 列，其余是填充列——但缓冲区必须容纳整个
  `mb_storage_width()`，否则内核会越界写；
- 渲染调用返回前不要从其他线程触碰输出缓冲区；
- 一个 context 不要并发调用；
- `mb_last_error()` 是线程局部的，另一个线程读不到本线程的错误；
- `mb_render_frame` 的颜色/迭代输出可以只给一个，但不能同时为 NULL；
- `mb_cancel` 后的帧是部分渲染的，不要当作有效结果使用。

## 相关文件

- `include/mandelbrot_explorer/mandelbrot_explorer.h`、`include/mandelbrot_explorer/video.h`：
  带注释的 API 参考；
- `tests/c_api_smoke.c`、`tests/video_api_smoke.c`：可运行的用法示例（ctest 会执行）；
- `bindings/python`：Python 绑定与自测；
- `README.md`：构建与 CLI 使用。
