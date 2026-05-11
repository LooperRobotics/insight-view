// Application.hpp
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <GL/gl.h>  // 添加 OpenGL 类型
#include <turbojpeg.h>
#include <mutex>
#include "viewer/app/Config.hpp"
#include "viewer/video/IFrameSource.hpp"
#include "viewer/video/UvcExtensionUnit.hpp"
#include "viewer/video/Frame.hpp"
#include "viewer/sensor/IHidSensor.hpp"
#include "viewer/sensor/ImuSample.hpp"
#include "viewer/sensor/VioSample.hpp"

struct SDL_Window;
using SDL_GLContext = void*;

namespace viewer {
    class UvcExtensionUnit;
    class Application {
    public:
        struct VideoSettings {
            int selected_camera = 0;
            int resolution_index = 0;
            int fps = 1;
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
        };

        Application();
        ~Application();
        bool initialize(const std::string& config_path = "configs/default.json");
        void run();
        void shutdown();
        void onImage(int cam_id, uint8_t* data, size_t size,
                     int width, int height, unsigned int format,
                     uint64_t timestamp, uint64_t right_timestamp);
        void onImu(float ax, float ay, float az,
                float gx, float gy, float gz,
                uint64_t timestamp);
        void onVio(float px, float py, float pz,
                float qx, float qy, float qz, float qw,
                uint64_t timestamp);

    private:
        AppConfig config_;
        bool running_ = false;
        SDL_Window* window_ = nullptr;
        SDL_GLContext gl_context_ = nullptr;
        std::vector<CompressedFrame> latest_frames_;
        std::vector<bool> frame_ready_;                // 是否有新帧
        std::mutex img_mutex_;
        std::mutex sensor_mutex_;
        ImuSample latest_imu_;
        VioSample latest_vio_;
        tjhandle tjInstance_ = nullptr;

        // 新增纹理管理
        struct TextureInfo {
            GLuint id = 0;
            int width = 0;
            int height = 0;
            GLenum format = 0;   // 当前分配使用的 internalFormat
        };

        struct SensorSettings {
            bool enable_imu = true;
            bool enable_vio = true;
            int imu_rate = 400;
            int vio_rate = 30;
            bool raw_mode = false;
        } sensor_settings_;

        struct LayoutRect {
            float x, y, w, h;
        };
        struct UILayout {
            LayoutRect leftGrey   = {0.0f, 25.0f, 295.0f, 345.0f};
            LayoutRect rightGrey  = {0.0f, 370.0f, 295.0f, 345.0f};
            LayoutRect rgb        = {295.0f, 25.0f, 340.0f, 600.0f};
            LayoutRect depth      = {635.0f, 25.0f, 295.0f, 345.0f};
            LayoutRect sensor     = {295.0f, 625.0f, 800.0f, 90.0f};
            LayoutRect config     = {930.0f, 25.0f, 350.0f, 690.0f};
            LayoutRect settings   = {0.0f, 25.0f, 1280.0f, 690.0f};
            LayoutRect log        = {0.0f, 405.0f, 1280.0f, 315.0f};
        };
        static const UILayout kUILayout;
        
        std::vector<TextureInfo> textures_;
        std::vector<uint64_t> timestamps_;
        std::vector<VideoSettings> video_settings_;
        std::vector<uint8_t> rgb_scratch_;   // 复用的像素缓冲，避免每帧分配
        std::vector<uint8_t> rotated_rgb_scratch_;
        std::vector<uint8_t> rotated_grey_scratch_;
        std::vector<std::chrono::steady_clock::time_point> last_video_frame_times_;
        std::unique_ptr<UvcExtensionUnit> xu_controller_;
        std::vector<camera_params> initial_params_;
        std::string xu_device_path_;
        bool xu_available_ = false;
        bool show_toast = false;
        double toast_start_time = 0.0;
        std::string toast_message;

        int current_camera_index_ = 0;

        // UI显示管理
        bool show_video_window_ = true;
        bool show_sensor_window_ = true;
        bool show_config_ = true;
        bool show_settings_ = false;
        bool show_depth_window_ = true;
        bool show_log_window_ = false;
        bool lock_windows_ = true;
        bool auto_scroll_log_ = true;

        std::chrono::steady_clock::time_point last_frame_time_ = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point last_reconnect_attempt_ = std::chrono::steady_clock::time_point::min();

        // 私有方法
        void renderUI();
        void renderLogWindow(float scaleX, float scaleY);
        void renderConfigWindow(float scaleX, float scaleY);
        void updateTexture(const CompressedFrame& frame, size_t index);
        void depthToRGB(const uint16_t* depth, int width, int height, std::vector<uint8_t>& rgb);
        void renderCameraStatus(bool hasRecentFrame, const char* cameraLabel);
        int settingsIndexForDisplay(size_t index) const;
        int rotationAngleForDisplay(size_t index) const;
        void applySettings();
        void saveSettings();
        void loadSettings();
        bool initializeXuController();
        void saveInitialCameraParams();
        void restoreInitialCameraParams();
        bool resetCameraParams(uint8_t cam_id);
        bool ensureXuAvailable();
        void ShowToast();
    };
} // namespace viewer
