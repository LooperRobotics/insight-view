#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace viewer {

enum class PixelFormat {
    Unknown = 0,
    MJPEG,
    YUYV,
    GREY,
    RGB8,
    RGBA8,
    Z16
};

struct DeviceInfo {
    std::string id;
    std::string name;
    uint16_t vid = 0;
    uint16_t pid = 0;
    std::string path;
    bool connected = false;
};

} // namespace viewer
