#include "viewer/app/Config.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace viewer {
    AppConfig loadConfig(const std::string& path) {
        AppConfig cfg;
        // 先清空 streams，后续根据 JSON 决定
        cfg.streams.clear();

        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            // 文件不存在时使用默认 mock 流
            VideoStreamConfig cam0;
            cam0.id = "cam0";
            cam0.mode = "mock";
            cam0.device = "/dev/null";
            cam0.width = 1920;
            cam0.height = 1080;
            cam0.fps = 30;
            cam0.format = "MJPEG";
            cfg.streams.push_back(cam0);

            VideoStreamConfig cam1;
            cam1.id = "cam1";
            cam1.mode = "mock";
            cam1.device = "/dev/null";
            cam1.width = 1088;
            cam1.height = 1280;
            cam1.fps = 30;
            cam1.format = "GREY";
            cfg.streams.push_back(cam1);

            VideoStreamConfig cam2;
            cam2.id = "cam2";
            cam2.mode = "mock";
            cam2.device = "/dev/null";
            cam2.width = 1088;
            cam2.height = 1280;
            cam2.fps = 30;
            cam2.format = "GREY";
            cfg.streams.push_back(cam2);
            return cfg;
        }

        auto j = nlohmann::json::parse(ifs);
        // 解析 app ...
        if (j.contains("app")) {
            auto& app = j["app"];
            cfg.app_name = app.value("name", cfg.app_name);
            cfg.enable_docking = app.value("enable_docking", cfg.enable_docking);
            cfg.enable_viewports = app.value("enable_viewports", cfg.enable_viewports);
            cfg.vsync = app.value("vsync", cfg.vsync);
        }

        // 解析 video.streams
        if (j.contains("video") && j["video"].contains("streams")) {
            for (auto& s : j["video"]["streams"]) {
                VideoStreamConfig vc;
                vc.id = s.value("id", "");
                vc.mode = s.value("mode", "mock");
                vc.device = s.value("device", "");
                vc.vid = static_cast<uint16_t>(s.value("vid", 0));
                vc.pid = static_cast<uint16_t>(s.value("pid", 0));
                vc.serial = s.value("serial", "");
                vc.device_path = s.value("device_path", "");
                vc.width = s.value("width", 1280);
                vc.height = s.value("height", 720);
                vc.fps = s.value("fps", 30);
                vc.format = s.value("format", "MJPEG");
                vc.exposure_time = s.value("exposure_time", vc.exposure_time);
                vc.exposure_gain = s.value("exposure_gain", vc.exposure_gain);
                vc.backlight_comp = s.value("backlight_comp", vc.backlight_comp);
                vc.brightness = s.value("brightness", vc.brightness);
                vc.contrast = s.value("contrast", vc.contrast);
                vc.gamma_dark = s.value("gamma_dark", vc.gamma_dark);
                vc.hue = s.value("hue", vc.hue);
                vc.saturation = s.value("saturation", vc.saturation);
                vc.sharpness = s.value("sharpness", vc.sharpness);
                vc.auto_white_balance = s.value("auto_white_balance", vc.auto_white_balance);
                vc.white_balance = s.value("white_balance", vc.white_balance);
                vc.decimation = s.value("decimation", vc.decimation);
                vc.rotation = s.value("rotation", vc.rotation);
                vc.interface_ = s.value("interface", vc.interface_);
                cfg.streams.push_back(vc);
            }
        }

        // 解析 sensor
        if (j.contains("sensor")) {
            auto& s = j["sensor"];
            cfg.sensor.mode = s.value("mode", "mock");
            cfg.sensor.vid = static_cast<uint16_t>(s.value("vid", 0));
            cfg.sensor.pid = static_cast<uint16_t>(s.value("pid", 0));
            cfg.sensor.imu_interface = s.value("imu_interface", cfg.sensor.imu_interface);
            cfg.sensor.vio_interface = s.value("vio_interface", cfg.sensor.vio_interface);
            cfg.sensor.imu_device = s.value("imu_device", "");
            cfg.sensor.vio_device = s.value("vio_device", "");
            cfg.sensor.imu_hz = s.value("imu_hz", 200);
            cfg.sensor.vio_hz = s.value("vio_hz", 60);
        }

        // 如果 streams 仍为空，添加一个默认 mock 流（可选）
        if (cfg.streams.empty()) {
            VideoStreamConfig cam0;
            cam0.id = "cam0";
            cam0.mode = "mock";
            cam0.width = 640;
            cam0.height = 480;
            cam0.fps = 30;
            cam0.format = "MJPEG";
            cfg.streams.push_back(cam0);
        }

        return cfg;
    }
} // namespace viewer
