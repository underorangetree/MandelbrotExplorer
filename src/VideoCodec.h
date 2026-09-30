#pragma once
#include <string>
#include <opencv2/videoio.hpp>

// Codec name -> OpenCV fourcc, shared by the CLI and the video API so both
// accept exactly the same names.
namespace mandelbrot {

inline auto video_codec_known(const std::string& name) -> bool {
    return name == "mp4v" || name == "h264" || name == "avc1" || name == "h265" ||
           name == "hevc" || name == "vp9" || name == "av1" || name == "mjpg";
}

inline auto video_codec_fourcc(const std::string& name) -> int {
    if (name == "h264" || name == "avc1") {
        return cv::VideoWriter::fourcc('a', 'v', 'c', '1');
    }
    if (name == "h265" || name == "hevc") {
        return cv::VideoWriter::fourcc('h', 'e', 'v', '1');
    }
    if (name == "vp9") {
        return cv::VideoWriter::fourcc('v', 'p', '0', '9');
    }
    if (name == "av1") {
        return cv::VideoWriter::fourcc('a', 'v', '0', '1');
    }
    if (name == "mjpg") {
        return cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    }
    return cv::VideoWriter::fourcc('m', 'p', '4', 'v');
}

} // namespace mandelbrot
