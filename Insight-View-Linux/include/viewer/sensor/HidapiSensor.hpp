#pragma once
#include "viewer/sensor/IHidSensor.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <hidapi.h>

namespace viewer {

struct HidSensorConfig {
    std::string imu_path;   // 如 "/dev/hidraw0"
    std::string vio_path;
    std::string calib_path; // 可选
};

class HidapiSensor : public IHidSensor {
public:
    explicit HidapiSensor(const HidSensorConfig& cfg);
    ~HidapiSensor();

    bool start() override;
    void stop() override;
    bool pollImu(ImuSample& sample) override;
    bool pollVio(VioSample& sample) override;
    std::string name() const override;

private:
    void imuLoop();
    void vioLoop();

    HidSensorConfig cfg_;
    std::atomic<bool> running_{false};
    std::thread imuThread_;
    std::thread vioThread_;

    hid_device* imuDev_ = nullptr;
    hid_device* vioDev_ = nullptr;

    std::mutex imuMutex_;
    std::queue<ImuSample> imuQueue_;
    std::mutex vioMutex_;
    std::queue<VioSample> vioQueue_;
};

} // namespace viewer