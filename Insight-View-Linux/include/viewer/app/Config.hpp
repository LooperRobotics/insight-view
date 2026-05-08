#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace viewer {
    struct VideoStreamConfig {
        std::string id;
        std::string mode;
        std::string device;
        uint16_t vid = 0;
        uint16_t pid = 0;
        std::string serial;
        std::string device_path;
        int width = 1280;
        int height = 720;
        int fps = 30;
        std::string format = "MJPEG";
        float exposure_time = 0.0f;
        float exposure_gain = 0.0f;
        bool backlight_comp = true;
        float brightness = 0.0f;
        float contrast = 0.0f;
        float gamma_dark = 0.0f;
        float hue = 0.0f;
        float saturation = 0.0f;
        float sharpness = 0.0f;
        bool auto_white_balance = false;
        float white_balance = 0.0f;
        float decimation = 0.0f;
        float rotation = 0.0f;
        int interface_ = 0;
    };
    struct SensorConfig {
        std::string mode = "mock";
        uint16_t vid = 0;
        uint16_t pid = 0;
        int imu_interface = 0;
        int vio_interface = 1;
        std::string imu_device;
        std::string vio_device;
        int imu_hz = 200;
        int vio_hz = 60;
    };
    struct AppConfig {
        std::string app_name = "UVC Viewer";
        bool enable_docking = true;
        bool enable_viewports = true;
        bool vsync = true;
        std::vector<VideoStreamConfig> streams;
        SensorConfig sensor;
    };
    AppConfig loadConfig(const std::string& path);
} // namespace viewer
