#pragma once
#include <cstdint>
namespace viewer {
struct VioSample {
    uint64_t host_ts_ns = 0;
    uint64_t device_ts_ns = 0;
    float position[3] = {0.f, 0.f, 0.f};
    float orientation_xyzw[4] = {0.f, 0.f, 0.f, 1.f};
    float velocity[3] = {0.f, 0.f, 0.f};
    int tracking_state = 0;
    bool valid = false;
};
} // namespace viewer
