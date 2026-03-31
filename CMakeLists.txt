cmake_minimum_required(VERSION 3.10)
project(MandelbrotExplorer VERSION 0.4.0
        DESCRIPTION "A Mandelbrot set explorer using OpenCV"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCV REQUIRED)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/ExitStatus.h
    src/Mandelbrot.cpp
    src/Mandelbrot.h
    src/ThreadPool.cpp
    src/ThreadPool.h
)

target_link_libraries(${PROJECT_NAME} ${OpenCV_LIBS})

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Werror
    -march=native
    $<$<CONFIG:RELEASE>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    $<$<CONFIG:DEBUG>:-g -O0>
    $<$<CONFIG:MinSizeRel>:-Os>
)