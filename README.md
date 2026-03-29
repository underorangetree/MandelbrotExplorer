# MandelbrotExplorer

高性能的Mandelbrot集合视频制作器，使用 C++20 编写，结合 SIMD、多线程管理与 OpenCV 库，实现视频生成。

![Static Badge](https://img.shields.io/badge/C++-20-%2300599C?logo=cplusplus)
![GitHub repo size](https://img.shields.io/github/repo-size/UnderOrangeTree/MandelbrotExplorer)

~~首先，这个仓库是个史山，请不要在这个上花费太多的时间~~

## 特性
- 🚀 SIMD 加速计算 – 使用 AVX、NEON等SIMD指令集，每次迭代同时处理多个像素点。
- 🧵 多线程渲染 – 按水平条带分割图像，利用线程池处理任务。
- 🎨 彩色可视化 – 采用近似<6000K黑体辐射颜色映射。
- 🎥 视频导出 – 生成 MP4 视频，支持平滑缩放动画。

## 环境要求
- 支持 C++20 的编译器
- CMake 3.10 或更高版本
- OpenCV 4.x（需包含开发文件）

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
- 计算并渲染分辨率 1920×1080、时长 10 秒的曼德勃罗集动画。
- 将视频保存为当前目录下的 mandelbrot.mp4。
- 在终端打印渲染进度和性能统计。

## 参数调整

所有渲染参数均以常量形式定义在 main.cpp 中，修改后重新编译即可生效：
- WIDTH、HEIGHT – 输出视频分辨率。
- MAX_ITER – 单点最大迭代次数（值越高细节越丰富，速度越慢）。
- FPS – 输出视频帧率。
- DURATION_SECONDS – 动画总时长（秒）。
- OUTPUT_FILE – 输出视频文件名。
- 缩放动画曲线与目标中心点也在 main.cpp 中硬编码（位于 zoom 计算与 setView 调用处）。

## 许可证

~~史山要什么许可证？~~