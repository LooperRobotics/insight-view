#pragma once
#include <cstdint>
#include <vector>
#include "viewer/core/Types.hpp"

namespace viewer {
struct CompressedFrame {
    uint64_t host_rx_ts_ns = 0;
    uint64_t device_ts_ns = 0;
    uint64_t device_right_ts_ns = 0;
    uint32_t sequence = 0;
    PixelFormat format = PixelFormat::Unknown;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;
};
} // namespace viewer
