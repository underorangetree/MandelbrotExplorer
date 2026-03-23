# MandelbrotExplorer

高性能的Mandelbrot集合视频制作器，使用 C++20 编写，结合 AVX2 指令集、多线程与 OpenCV 库，实现视频生成。

![Static Badge](https://img.shields.io/badge/C++-20-%2300599C?logo=cplusplus)
![GitHub repo size](https://img.shields.io/github/repo-size/UnderOrangeTree/MandelbrotExplorer)

~~首先，这个仓库是个史山，请不要在这个上花费太多的时间~~

## 特性
- 🚀 AVX2 加速计算 – 使用 256 位 SIMD 指令，每次迭代同时处理 4 个像素点。
- 🧵 多线程渲染 – 按水平条带分割图像，每个 CPU 核心处理一个条带。
- 🎨 彩色可视化 – 采用黑体辐射颜色映射以及泛光效果。
- 🎥 视频导出 – 生成 MP4 视频，支持平滑缩放动画。
- 📊 性能分析 – 输出各阶段耗时 CSV 文件，方便性能调优。

## 环境要求
- 支持 C++20 且支持 AVX2 的编译器
- CMake 3.10 或更高版本
- OpenCV 4.x（需包含开发文件）
- 支持 AVX 指令集的 CPU

## 编译和运行

```bash
git clone https://github.com/UnderOrangeTree/MandelbrotExplorer.git # 克隆仓库
cd MandelbrotExplorer # 进入仓库目录
mkdir build && cd build
cmake .. # 配置
make # 编译
```

```bash
./MandelbrotExplorer # 运行
```

程序的默认行为：
- 计算并渲染分辨率1920×1080、时长10秒的曼德勃罗集动画。
- 将视频保存为当前目录下的 mandelbrot.mp4。
- 在终端打印渲染进度和性能统计。
- 生成 timings.csv 文件，包含各阶段详细耗时数据。

## 参数调整

所有渲染参数均以常量形式定义在 main.cpp 中，修改后重新编译即可生效：
- WIDTH、HEIGHT – 输出视频分辨率（因 SIMD 对齐要求，宽度需为 8 的倍数）。
- MAX_ITER – 单点最大迭代次数（值越高细节越丰富，速度越慢）。
- FPS – 输出视频帧率。
- DURATION_SECONDS – 动画总时长（秒）。
- OUTPUT_FILE – 输出视频文件名。
- 缩放动画曲线与目标中心点也在 main.cpp 中硬编码（位于 zoom 计算与 setView 调用处）。

## 性能分析

程序运行时会在 timings.csv 中记录三个阶段的时间（单位：微秒）：
- Generate Stage – 计算逃逸迭代次数。
- Color Stage – 将迭代次数转换为 RGB 颜色。
- Bloom Stage – 应用泛光效果。

利用这些数据可以定位性能瓶颈，并根据需要调整参数或优化代码。

## 许可证

~~史山要什么许可证？~~