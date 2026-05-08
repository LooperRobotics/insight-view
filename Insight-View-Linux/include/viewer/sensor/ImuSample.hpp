#pragma once
#include <cstdint>
namespace viewer {
struct ImuSample {
    uint64_t host_ts_ns = 0;
    uint64_t device_ts_ns = 0;
    float accel[3] = {0.f, 0.f, 0.f};
    float gyro[3] = {0.f, 0.f, 0.f};
    float temperature = 0.f;
    bool valid = false;
};
} // namespace viewer
