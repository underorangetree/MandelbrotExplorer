#pragma once
#include <cstdint>
enum ExitStatus : std::uint8_t {
    Success = 0,
    Bug,
    InvalidArgument,
    VideoWriterError,
    MemoryAllocationError,
    RuntimeError
};