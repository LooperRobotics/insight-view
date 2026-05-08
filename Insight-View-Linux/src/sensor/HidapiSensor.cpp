#include "viewer/sensor/HidapiSensor.hpp"
#include "viewer/core/Time.hpp"
#include <thread>
#include <chrono>
#include <cstring>

namespace viewer {
    HidapiSensor::HidapiSensor(const HidSensorConfig& cfg) : cfg_(cfg) {}

    HidapiSensor::~HidapiSensor() {
        stop();
    }

    std::string HidapiSensor::name() const {
        return "HidapiSensor";
    }

    bool HidapiSensor::start() {
        if (running_) return true;
        running_ = true;
        imuThread_ = std::thread(&HidapiSensor::imuLoop, this);
        vioThread_ = std::thread(&HidapiSensor::vioLoop, this);
        return true;
    }

    void HidapiSensor::stop() {
        running_ = false;
        if (imuThread_.joinable()) imuThread_.join();
        if (vioThread_.joinable()) vioThread_.join();
        // 关闭设备
        if (imuDev_) { hid_close(imuDev_); imuDev_ = nullptr; }
        if (vioDev_) { hid_close(vioDev_); vioDev_ = nullptr; }
    }

    void HidapiSensor::imuLoop() {
        while (running_) {
            if (!imuDev_) {
                imuDev_ = hid_open_path(cfg_.imu_path.c_str());
                if (!imuDev_) {
                    printf("[HID] Failed to open IMU device %s\n", cfg_.imu_path.c_str());
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                printf("[HID] Successfully opened IMU device %s\n", cfg_.imu_path.c_str());
                hid_set_nonblocking(imuDev_, 1);  // 设置为非阻塞以便退出线程
            }

            uint8_t buf[16];  // 根据你的报告大小调整
            int ret = hid_read(imuDev_, buf, sizeof(buf));
            if (ret < 0) {
                // 错误，可能是设备断开
                hid_close(imuDev_);
                imuDev_ = nullptr;
                continue;
            } else if (ret == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            // 根据你的 HID 报告格式解析
            // 假设你的 IMU 报告是 14 字节（3轴加速度+3轴角速度+时间戳）
            if (ret >= 16) {  // 完整报告应为 16 字节
                ImuSample sample;
                sample.host_ts_ns = nowNs();

                // 提取加速度（假设单位需要除以 1000 以得到 m/s²，根据实际调整）
                sample.accel[0] = (int16_t)(buf[0] | (buf[1] << 8)) / 1000.0f;
                sample.accel[1] = (int16_t)(buf[2] | (buf[3] << 8)) / 1000.0f;
                sample.accel[2] = (int16_t)(buf[4] | (buf[5] << 8)) / 1000.0f;

                // 提取角速度（假设单位需要除以 100 以得到 rad/s，根据实际调整）
                sample.gyro[0] = (int16_t)(buf[6] | (buf[7] << 8)) / 100.0f;
                sample.gyro[1] = (int16_t)(buf[8] | (buf[9] << 8)) / 100.0f;
                sample.gyro[2] = (int16_t)(buf[10] | (buf[11] << 8)) / 100.0f;

                // 提取时间戳（假设为微秒，转换为纳秒；若为毫秒则乘以 1e6）
                uint32_t ts;
                memcpy(&ts, buf + 12, 4);
                sample.device_ts_ns = (uint64_t)ts * 1000;  // 微秒 -> 纳秒

                sample.temperature = 0;  // 未提供
                sample.valid = true;

                // printf("IMU parsed: accel=(%f,%f,%f) gyro=(%f,%f,%f) ts=%u\n",
                //     sample.accel[0], sample.accel[1], sample.accel[2],
                //     sample.gyro[0], sample.gyro[1], sample.gyro[2], ts);

                std::lock_guard<std::mutex> lock(imuMutex_);
                if (imuQueue_.size() > 10) imuQueue_.pop();
                imuQueue_.push(sample);
            }
        }
    }

    bool HidapiSensor::pollImu(ImuSample& sample) {
        std::lock_guard<std::mutex> lock(imuMutex_);
        if (imuQueue_.empty()) {
            static int empty_count = 0;
            if (++empty_count % 30 == 0) printf("[HID] IMU queue empty\n");
            return false;
        }
        sample = imuQueue_.front();
        imuQueue_.pop();
        return true;
    }

    void HidapiSensor::vioLoop() {
        while (running_) {
            if (!vioDev_) {
                vioDev_ = hid_open_path(cfg_.vio_path.c_str());
                if (!vioDev_) {
                     printf("[HID] Failed to open VIO device %s\n", cfg_.vio_path.c_str());
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                printf("[HID] Successfully opened VIO device %s\n", cfg_.vio_path.c_str());
                hid_set_nonblocking(vioDev_, 1);
            }

            uint8_t buf[40];  // 确保足够大
            int ret = hid_read(vioDev_, buf, sizeof(buf));
            if (ret < 0) {
                hid_close(vioDev_);
                vioDev_ = nullptr;
                continue;
            } else if (ret == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            if (ret >= 36) {  // 完整报告为 36 字节
                VioSample sample;
                sample.host_ts_ns = nowNs();

                // 提取时间戳
                uint32_t sec, nsec;
                memcpy(&sec, buf, 4);
                memcpy(&nsec, buf + 4, 4);
                sample.device_ts_ns = (uint64_t)sec * 1000000000ULL + nsec;

                // 提取位置（3个float）
                memcpy(sample.position, buf + 8, 12);  // px, py, pz

                // 提取四元数（4个float）
                memcpy(sample.orientation_xyzw, buf + 20, 16);  // qx, qy, qz, qw

                // 速度未提供，保持为 0
                // tracking_state 未提供，可设为 1 表示有效
                sample.tracking_state = 1;
                sample.valid = true;

                // printf("VIO parsed: pos=(%f,%f,%f) quat=(%f,%f,%f,%f) ts=%lu.%09lu\n",
                //     sample.position[0], sample.position[1], sample.position[2],
                //     sample.orientation_xyzw[0], sample.orientation_xyzw[1],
                //     sample.orientation_xyzw[2], sample.orientation_xyzw[3],
                //     (unsigned long)sec, (unsigned long)nsec);

                std::lock_guard<std::mutex> lock(vioMutex_);
                if (vioQueue_.size() > 10) vioQueue_.pop();
                vioQueue_.push(sample);
            }
        }
    }

    bool HidapiSensor::pollVio(VioSample& sample) {
        std::lock_guard<std::mutex> lock(vioMutex_);
        if (vioQueue_.empty()) {
            static int empty_count = 0;
            if (++empty_count % 30 == 0) printf("[HID] VIO queue empty\n");
            return false;
        }
        sample = vioQueue_.front();
        vioQueue_.pop();
        return true;
    }
}