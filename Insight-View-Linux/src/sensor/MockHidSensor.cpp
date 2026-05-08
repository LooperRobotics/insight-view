#include "viewer/sensor/MockHidSensor.hpp"
#include "viewer/core/Time.hpp"
#include <cmath>

namespace viewer {
    MockHidSensor::MockHidSensor(MockHidConfig cfg) : cfg_(std::move(cfg)) {}

    bool MockHidSensor::start() {
        running_ = true; start_ts_ns_ = nowNs(); last_imu_ts_ns_ = start_ts_ns_; last_vio_ts_ns_ = start_ts_ns_; return true; }
    
    void MockHidSensor::stop() {
        running_ = false;
    }
    
    std::string MockHidSensor::name() const {
        return "MockHidSensor";
    }
    
    bool MockHidSensor::pollImu(ImuSample& sample) {
        if (!running_) return false;
        const uint64_t interval_ns = 1000000000ull / static_cast<uint64_t>(cfg_.imu_hz);
        const uint64_t now = nowNs();
        if (now - last_imu_ts_ns_ < interval_ns) return false;
        last_imu_ts_ns_ = now;
        const double t = (now - start_ts_ns_) / 1e9;
        sample.host_ts_ns = now; sample.device_ts_ns = now - 500000ull;
        sample.accel[0] = static_cast<float>(std::sin(t));
        sample.accel[1] = static_cast<float>(std::cos(t*0.7));
        sample.accel[2] = 9.8f + static_cast<float>(0.1 * std::sin(t*0.3));
        sample.gyro[0] = static_cast<float>(0.5 * std::sin(t*1.7));
        sample.gyro[1] = static_cast<float>(0.3 * std::cos(t*1.3));
        sample.gyro[2] = static_cast<float>(0.8 * std::sin(t*0.9));
        sample.temperature = 36.5f + static_cast<float>(0.5 * std::sin(t*0.05));
        sample.valid = true; return true;
    }

    bool MockHidSensor::pollVio(VioSample& sample) {
        if (!running_) return false;
        const uint64_t interval_ns = 1000000000ull / static_cast<uint64_t>(cfg_.vio_hz);
        const uint64_t now = nowNs();
        if (now - last_vio_ts_ns_ < interval_ns) return false;
        last_vio_ts_ns_ = now;
        const double t = (now - start_ts_ns_) / 1e9;
        sample.host_ts_ns = now; sample.device_ts_ns = now - 1000000ull;
        sample.position[0] = static_cast<float>(std::cos(t));
        sample.position[1] = static_cast<float>(std::sin(t));
        sample.position[2] = static_cast<float>(0.2 * std::sin(t*0.5));
        sample.orientation_xyzw[2] = static_cast<float>(std::sin(t*0.5));
        sample.orientation_xyzw[3] = static_cast<float>(std::cos(t*0.5));
        sample.velocity[0] = static_cast<float>(-std::sin(t));
        sample.velocity[1] = static_cast<float>( std::cos(t));
        sample.velocity[2] = static_cast<float>(0.1 * std::cos(t*0.5));
        sample.tracking_state = 1; sample.valid = true; return true;
    }
} // namespace viewer
