#pragma once
#include <atomic>
#include <string>
#include "viewer/sensor/IHidSensor.hpp"
namespace viewer {
struct MockHidConfig {
    int imu_hz = 200;
    int vio_hz = 60;
};
class MockHidSensor : public IHidSensor {
public:
    explicit MockHidSensor(MockHidConfig cfg);
    bool start() override;
    void stop() override;
    bool pollImu(ImuSample& sample) override;
    bool pollVio(VioSample& sample) override;
    std::string name() const override;
private:
    MockHidConfig cfg_;
    std::atomic<bool> running_{false};
    uint64_t start_ts_ns_ = 0;
    uint64_t last_imu_ts_ns_ = 0;
    uint64_t last_vio_ts_ns_ = 0;
};
} // namespace viewer
