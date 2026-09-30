cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0074 NEW)
project(MandelbrotExplorer VERSION 0.5.0
        DESCRIPTION "A Mandelbrot set explorer using OpenCV"
        LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCV REQUIRED COMPONENTS core videoio)
find_package(Threads REQUIRED)

add_library(mandelbrot_explorer_core STATIC
    src/CommandLine.cpp
    src/CommandLine.h
    src/ExitStatus.h
    src/MandelbrotLimits.h
    src/Mandelbrot.cpp
    src/Mandelbrot.h
    src/MandelbrotKernel.cpp
    src/MandelbrotKernel.h
    src/MandelbrotKernel_scalar.cpp
    src/ThreadPool.cpp
    src/ThreadPool.h
)
target_include_directories(mandelbrot_explorer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The CLI prints this for --version; single-sourced from PROJECT_VERSION.
target_compile_definitions(mandelbrot_explorer_core PRIVATE MANDELBROT_EXPLORER_VERSION="${PROJECT_VERSION}")
# The renderer core is OpenCV-free; only the app and the tests use OpenCV.
target_link_libraries(mandelbrot_explorer_core PUBLIC Threads::Threads)
# The core is linked into the shared C ABI library, so it must be position independent.
set_target_properties(mandelbrot_explorer_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Keep every kernel on the exact operation sequence written in the source.
# Otherwise GCC/Clang may contract a*b+c into an FMA in one kernel but not in
# another, and the kernels stop being bit-identical to the scalar reference.
if(NOT MSVC)
    set_source_files_properties(src/MandelbrotKernel_scalar.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

# Per-architecture kernels: only the kernel translation unit gets the arch flags,
# so the rest of the binary stays on the baseline instruction set and can run on
# CPUs that lack the extension. select_kernel() dispatches at runtime.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _mandelbrot_arch)
if(_mandelbrot_arch MATCHES "x86_64|amd64|x64|i[3-6]86")
    target_sources(mandelbrot_explorer_core PRIVATE
        src/MandelbrotKernel_avx2.cpp
        src/MandelbrotKernel_avx512.cpp)
    if(MSVC)
        set_source_files_properties(src/MandelbrotKernel_avx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
        set_source_files_properties(src/MandelbrotKernel_avx512.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX512")
    else()
        set_source_files_properties(src/MandelbrotKernel_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;-ffp-contract=off")
        set_source_files_properties(src/MandelbrotKernel_avx512.cpp PROPERTIES COMPILE_OPTIONS "-mavx512f;-mfma;-ffp-contract=off")
    endif()
elseif((_mandelbrot_arch MATCHES "aarch64|arm64") AND NOT MSVC)
    target_sources(mandelbrot_explorer_core PRIVATE src/MandelbrotKernel_neon.cpp)
    set_source_files_properties(src/MandelbrotKernel_neon.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

# Shared library with a C ABI, callable from other languages through FFI.
add_library(mandelbrot_explorer SHARED
    src/MandelbrotC.cpp
    include/mandelbrot_explorer/mandelbrot_explorer.h)
target_link_libraries(mandelbrot_explorer PRIVATE mandelbrot_explorer_core)
target_include_directories(mandelbrot_explorer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(mandelbrot_explorer PRIVATE
    MANDELBROT_EXPLORER_BUILD
    MANDELBROT_EXPLORER_VERSION="${PROJECT_VERSION}")
set_target_properties(mandelbrot_explorer PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

include(GNUInstallDirs)
install(TARGETS mandelbrot_explorer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/mandelbrot_explorer/mandelbrot_explorer.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mandelbrot_explorer)

# Video export lives in a separate OpenCV-based library so the core C ABI stays
# dependency-free.
add_library(mandelbrot_explorer_video SHARED
    src/MandelbrotVideoC.cpp
    include/mandelbrot_explorer/video.h)
target_link_libraries(mandelbrot_explorer_video PRIVATE mandelbrot_explorer_core ${OpenCV_LIBS})
target_include_directories(mandelbrot_explorer_video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(mandelbrot_explorer_video PRIVATE MANDELBROT_EXPLORER_VIDEO_BUILD)
set_target_properties(mandelbrot_explorer_video PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
install(TARGETS mandelbrot_explorer_video
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/mandelbrot_explorer/video.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mandelbrot_explorer)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE mandelbrot_explorer_core ${OpenCV_LIBS})

foreach(target mandelbrot_explorer_core mandelbrot_explorer mandelbrot_explorer_video ${PROJECT_NAME})
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            $<$<CONFIG:RELEASE>:/O2>
            $<$<CONFIG:RelWithDebInfo>:/O2 /Zi>
            $<$<CONFIG:DEBUG>:/Zi /Od>
            $<$<CONFIG:MinSizeRel>:/O1>
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Werror
            $<$<CONFIG:RELEASE>:-O2>
            $<$<CONFIG:RelWithDebInfo>:-O2 -g>
            $<$<CONFIG:DEBUG>:-g -O0>
            $<$<CONFIG:MinSizeRel>:-Os>
        )
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(mandelbrot_tests tests/mandelbrot_tests.cpp)
    target_link_libraries(mandelbrot_tests PRIVATE mandelbrot_explorer_core ${OpenCV_LIBS})
    add_test(NAME unit COMMAND mandelbrot_tests)

    add_executable(mandelbrot_benchmark tests/benchmark.cpp)
    target_link_libraries(mandelbrot_benchmark PRIVATE mandelbrot_explorer_core ${OpenCV_LIBS})

    add_executable(c_api_smoke tests/c_api_smoke.c)
    target_link_libraries(c_api_smoke PRIVATE mandelbrot_explorer)
    set_target_properties(c_api_smoke PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    add_test(NAME c_api COMMAND c_api_smoke)

    add_executable(video_api_smoke tests/video_api_smoke.c)
    target_link_libraries(video_api_smoke PRIVATE mandelbrot_explorer_video)
    set_target_properties(video_api_smoke PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    add_test(NAME video_api COMMAND video_api_smoke)
    # Exit code 3 = no usable video backend: skipped.
    set_tests_properties(video_api PROPERTIES SKIP_RETURN_CODE 3)

    # Python binding self-test (skipped with exit code 3 when numpy is absent).
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME python_binding
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/selftest.py
                         --library $<TARGET_FILE:mandelbrot_explorer>)
        set_tests_properties(python_binding PROPERTIES SKIP_RETURN_CODE 3)
    endif()

    add_test(NAME cli_help COMMAND ${PROJECT_NAME} --help)
    add_test(NAME cli_version COMMAND ${PROJECT_NAME} --version)

    add_test(NAME cli_invalid_size COMMAND ${PROJECT_NAME} -s 0x0)
    set_tests_properties(cli_invalid_size PROPERTIES WILL_FAIL TRUE)

    # On x86 the NEON kernel is not compiled in: run() must reject it before
    # opening the video writer (on aarch64, NEON is the native kernel).
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64|i.86")
        add_test(NAME cli_kernel_unavailable COMMAND ${PROJECT_NAME} --kernel neon)
        set_tests_properties(cli_kernel_unavailable PROPERTIES WILL_FAIL TRUE)
    endif()

    add_test(NAME cli_small_render COMMAND ${PROJECT_NAME} -s 320x240 --maxiter 16 --fps 1 --duration 1 -o ctest_out.mp4)
    # ExitStatus::VideoWriterError == 3: treated as "skipped" so the test does not
    # fail on OpenCV builds without a usable video writer backend.
    set_tests_properties(cli_small_render PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        SKIP_RETURN_CODE 3)
endif()
