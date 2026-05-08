#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <GL/gl.h>
#include <dlfcn.h>
#include <hidapi.h>
#include <inttypes.h>
#include <linux/videodev2.h>
#include <limits>
#include <cstdarg>
#include <nlohmann/json.hpp>
#include "viewer/app/Application.hpp"
#include "viewer/Insight_9_receive.h"
#include "viewer/app/UVCScanner.hpp"
#include "viewer/video/MockVideoSource.hpp"
#include "viewer/sensor/MockHidSensor.hpp"
#include "viewer/core/Types.hpp"
#include "viewer/core/Time.hpp"
#include "viewer/video/UvcExtensionUnit.hpp"
#include <fstream>
// #include "viewer/video/V4l2VideoSource.hpp"
// #include "viewer/sensor/HidapiSensor.hpp"


// 将角度标准化为有效档位：-90, 0, 90, 180
static int normalizeRotationAngle(float angleDeg) {
    // 先取整并映射到 [0, 360)
    int angle = static_cast<int>(std::round(angleDeg)) % 360;
    if (angle < 0) angle += 360;

    // 确定最近的有效角度（0, 90, 180, 270）
    if (angle >= 45 && angle < 135) return 90;
    if (angle >= 135 && angle < 225) return 180;
    if (angle >= 225 && angle < 315) return 270;
    return 0;
}

// 旋转 RGB 图像（顺时针旋转 angle 度，angle 必须为 0/90/180/270）
static void rotateRGB(const uint8_t* src, int srcW, int srcH,
                      std::vector<uint8_t>& dst, int angle) {
    if (angle == 0) {
        dst.assign(src, src + srcW * srcH * 3);
        return;
    }

    int dstW = (angle == 90 || angle == 270) ? srcH : srcW;
    int dstH = (angle == 90 || angle == 270) ? srcW : srcH;
    dst.assign(dstW * dstH * 3, 0);

    for (int y = 0; y < srcH; ++y) {
        for (int x = 0; x < srcW; ++x) {
            const uint8_t* s = src + (y * srcW + x) * 3;
            int dstX, dstY;
            if (angle == 90) {
                dstX = srcH - 1 - y;
                dstY = x;
            } else if (angle == 180) {
                dstX = srcW - 1 - x;
                dstY = srcH - 1 - y;
            } else { // angle == 270
                dstX = y;
                dstY = srcW - 1 - x;
            }
            uint8_t* d = dst.data() + (dstY * dstW + dstX) * 3;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    }
}

static void rotateGrey(const uint8_t* src, int srcW, int srcH,
                       std::vector<uint8_t>& dst, int angle) {
    if (angle == 0) {
        dst.assign(src, src + srcW * srcH);
        return;
    }

    const int dstW = (angle == 90 || angle == 270) ? srcH : srcW;
    const int dstH = (angle == 90 || angle == 270) ? srcW : srcH;
    dst.resize(dstW * dstH);

    for (int y = 0; y < srcH; ++y) {
        for (int x = 0; x < srcW; ++x) {
            const uint8_t value = src[y * srcW + x];
            int dstX = 0;
            int dstY = 0;
            if (angle == 90) {
                dstX = srcH - 1 - y;
                dstY = x;
            } else if (angle == 180) {
                dstX = srcW - 1 - x;
                dstY = srcH - 1 - y;
            } else { // angle == 270
                dstX = y;
                dstY = srcW - 1 - x;
            }
            dst[dstY * dstW + dstX] = value;
        }
    }
}

static void on_image(int cam_id, uint8_t *data, size_t size,
                     int width, int height, unsigned int format,
                     uint64_t timestamp, uint64_t right_timestamp,
                     void *userdata) {
    viewer::Application* app = static_cast<viewer::Application*>(userdata);
    app->onImage(cam_id, data, size, width, height, format, timestamp, right_timestamp);
}

static void on_imu(float ax, float ay, float az,
                   float gx, float gy, float gz,
                   uint64_t timestamp, void *userdata) {
    viewer::Application* app = static_cast<viewer::Application*>(userdata);
    app->onImu(ax, ay, az, gx, gy, gz, timestamp);
}

static void on_vio(float px, float py, float pz,
                   float qx, float qy, float qz, float qw,
                   uint64_t timestamp, void *userdata) {
    viewer::Application* app = static_cast<viewer::Application*>(userdata);
    app->onVio(px, py, pz, qx, qy, qz, qw, timestamp);
}

namespace {

constexpr int kUiFpsValues[] = {90, 60, 30, 20, 15, 10};
constexpr size_t kLogicalVideoSettingsCount = 2;
constexpr int kMaxLogLines = 2000;
constexpr int kMaxPendingLogLines = 4000;
static std::vector<std::string> g_log_lines;
static std::vector<std::string> g_pending_log_lines;
static std::mutex g_log_mutex;
static bool g_log_capture_enabled = true;
static FILE* g_log_file = nullptr;

FILE* getLogFileLocked() {
    if (!g_log_file) {
        g_log_file = std::fopen("runtime.log", "a");
    }
    return g_log_file;
}

void addLogInternal(const char* fmt, va_list args) {
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, args);

    std::lock_guard<std::mutex> lock(g_log_mutex);
    fputs(buf, stdout);
    fflush(stdout);
    // if (FILE* file = getLogFileLocked()) {
    //     fputs(buf, file);
    //     fflush(file);
    // }

    if (!g_log_capture_enabled) {
        return;
    }

    std::string text(buf);
    size_t start = 0;
    while (start < text.size()) {
        size_t pos = text.find('\n', start);
        std::string line;
        if (pos == std::string::npos) {
            line = text.substr(start);
            start = text.size();
        } else {
            line = text.substr(start, pos - start);
            start = pos + 1;
        }
        if (!line.empty()) {
            g_pending_log_lines.emplace_back(std::move(line));
            if (g_pending_log_lines.size() > kMaxPendingLogLines) {
                g_pending_log_lines.erase(g_pending_log_lines.begin(),
                    g_pending_log_lines.begin() + (g_pending_log_lines.size() - kMaxPendingLogLines));
            }
        }
    }
}

void addLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    addLogInternal(fmt, args);
    va_end(args);
}

void flushPendingLogs() {
    std::vector<std::string> pending;
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        pending.swap(g_pending_log_lines);
    }

    if (pending.empty()) {
        return;
    }

    for (auto& line : pending) {
        g_log_lines.emplace_back(std::move(line));
    }
    if (g_log_lines.size() > kMaxLogLines) {
        g_log_lines.erase(g_log_lines.begin(),
            g_log_lines.begin() + (g_log_lines.size() - kMaxLogLines));
    }
}

int readJsonInt(const nlohmann::json& object, const char* key, int defaultValue) {
    if (!object.is_object() || !object.contains(key)) {
        return defaultValue;
    }

    const auto& value = object[key];
    if (value.is_number_integer()) {
        return value.get<int>();
    }
    if (value.is_number_unsigned()) {
        return static_cast<int>(value.get<unsigned int>());
    }
    if (value.is_number_float()) {
        return static_cast<int>(std::round(value.get<double>()));
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? 1 : 0;
    }
    return defaultValue;
}

float readJsonFloat(const nlohmann::json& object, const char* key, float defaultValue) {
    if (!object.is_object() || !object.contains(key)) {
        return defaultValue;
    }

    const auto& value = object[key];
    if (value.is_number()) {
        return value.get<float>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? 1.0f : 0.0f;
    }
    return defaultValue;
}

bool readJsonBool(const nlohmann::json& object, const char* key, bool defaultValue) {
    if (!object.is_object() || !object.contains(key)) {
        return defaultValue;
    }

    const auto& value = object[key];
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number_integer()) {
        return value.get<int>() != 0;
    }
    if (value.is_number_unsigned()) {
        return value.get<unsigned int>() != 0;
    }
    if (value.is_number_float()) {
        return std::abs(value.get<double>()) > 0.0;
    }
    return defaultValue;
}

float mapControlValue(float value, float srcMin, float srcMax, float dstMin, float dstMax) {
    if (!(srcMin < srcMax)) {
        return dstMin;
    }

    const float clamped = std::clamp(value, srcMin, srcMax);
    const float normalized = (clamped - srcMin) / (srcMax - srcMin);
    return dstMin + normalized * (dstMax - dstMin);
}

uint8_t mapControlValueToByte(float value, float srcMin, float srcMax, float dstMin, float dstMax) {
    const float mapped = mapControlValue(value, srcMin, srcMax, dstMin, dstMax);
    const float lower = std::min(dstMin, dstMax);
    const float upper = std::max(dstMin, dstMax);
    return static_cast<uint8_t>(std::clamp(std::round(mapped), lower, upper));
}

int fpsValueToIndex(int fps) {
    for (int i = 0; i < static_cast<int>(sizeof(kUiFpsValues) / sizeof(kUiFpsValues[0])); ++i) {
        if (kUiFpsValues[i] == fps) {
            return i;
        }
    }
    return 2;
}

int fpsIndexToValue(int index) {
    if (index < 0 || index >= static_cast<int>(sizeof(kUiFpsValues) / sizeof(kUiFpsValues[0]))) {
        return kUiFpsValues[2];
    }
    return kUiFpsValues[index];
}

int resolutionToIndex(size_t streamIndex, int width, int height) {
    if (streamIndex == 0) {
        if (width == 3840 && height == 2160) return 0;
        if (width == 1088 && height == 1920) return 1;
        if (width == 1280 && height == 720) return 2;
        if (width == 640 && height == 480) return 3;
        return 2;
    }

    if (width == 1088 && height == 1280) return 0;
    if (width == 544 && height == 640) return 1;
    return 0;
}

void applyResolutionSelection(size_t streamIndex, int resolutionIndex, viewer::VideoStreamConfig& stream) {
    if (streamIndex == 0) {
        static constexpr int resW[] = {3840, 1088, 1280, 640};
        static constexpr int resH[] = {2160, 1920, 720, 480};
        if (resolutionIndex >= 0 && resolutionIndex < 4) {
            stream.width = resW[resolutionIndex];
            stream.height = resH[resolutionIndex];
        }
        return;
    }

    static constexpr int resW[] = {1088, 544};
    static constexpr int resH[] = {1280, 640};
    if (resolutionIndex >= 0 && resolutionIndex < 2) {
        stream.width = resW[resolutionIndex];
        stream.height = resH[resolutionIndex];
    }
}

void syncVideoSettingsFromStream(size_t streamIndex, const viewer::VideoStreamConfig& stream, viewer::Application::VideoSettings& vs) {
    vs.resolution_index = resolutionToIndex(streamIndex, stream.width, stream.height);
    vs.fps = fpsValueToIndex(stream.fps);
    vs.exposure_time = stream.exposure_time;
    vs.exposure_gain = stream.exposure_gain;
    vs.backlight_comp = stream.backlight_comp;
    vs.brightness = stream.brightness;
    vs.contrast = stream.contrast;
    vs.gamma_dark = stream.gamma_dark;
    vs.hue = stream.hue;
    vs.saturation = stream.saturation;
    vs.sharpness = stream.sharpness;
    vs.auto_white_balance = stream.auto_white_balance;
    vs.white_balance = stream.white_balance;
    vs.decimation = stream.decimation;
    vs.rotation = stream.rotation;
}

void syncStreamFromVideoSettings(size_t streamIndex, const viewer::Application::VideoSettings& vs, viewer::VideoStreamConfig& stream) {
    applyResolutionSelection(streamIndex, vs.resolution_index, stream);
    stream.fps = fpsIndexToValue(vs.fps);
    stream.exposure_time = vs.exposure_time;
    stream.exposure_gain = vs.exposure_gain;
    stream.backlight_comp = vs.backlight_comp;
    stream.brightness = vs.brightness;
    stream.contrast = vs.contrast;
    stream.gamma_dark = vs.gamma_dark;
    stream.hue = vs.hue;
    stream.saturation = vs.saturation;
    stream.sharpness = vs.sharpness;
    stream.auto_white_balance = vs.auto_white_balance;
    stream.white_balance = vs.white_balance;
    stream.decimation = vs.decimation;
    stream.rotation = vs.rotation;
}

viewer::camera_params toCameraParams(uint8_t camId, const viewer::Application::VideoSettings& vs) {
    viewer::camera_params params{};
    params.cam_id = camId;
    params.resolution = static_cast<uint8_t>(std::max(vs.resolution_index, 0));
    params.frame_rate = static_cast<uint8_t>(std::max(vs.fps, 0));
    params.exposure_time = mapControlValue(vs.exposure_time, 0.1f, 10000.0f, 0.0f, 0.03f);
    params.exposure_gain = mapControlValue(vs.exposure_gain, 1.0f, 10000.0f, 1.0f, 16.0f);
    params.backlight_comp = static_cast<uint8_t>(vs.backlight_comp ? 1 : 0);
    params.brightness = mapControlValue(vs.brightness, -64.0f, 64.0f, 0.0f, 127.0f);
    params.contrast = mapControlValue(vs.contrast, 0.0f, 100.0f, 0.0f, 1.9f);
    params.gamma_dark = mapControlValue(vs.gamma_dark, 100.0f, 500.0f, 1.0f, 4.0f);
    params.hue = mapControlValue(vs.hue, -180.0f, 180.0f, 0.0f, 87.0f);
    params.saturation = mapControlValue(vs.saturation, 0.0f, 100.0f, 0.0f, 1.999f);
    params.sharpness = mapControlValueToByte(vs.sharpness, 0.0f, 100.0f, 1.0f, 255.0f);
    params.auto_white_balance = static_cast<uint8_t>(vs.auto_white_balance ? 1 : 0);
    params.white_balance = mapControlValue(vs.white_balance, 2800.0f, 6500.0f, 1.0f, 3.0f);
    params.decimation = mapControlValueToByte(vs.decimation, 1.0f, 8.0f, 1.0f, 255.0f);
    params.rotation = mapControlValueToByte(vs.rotation, -90.0f, 180.0f, 1.0f, 255.0f);
    // printParams(params);
    return params;
}

std::string resolveXuDevicePath(const viewer::AppConfig& config) {
    using GetVideoDevFn = const char* (*)(int);
    const auto getVideoDev = reinterpret_cast<GetVideoDevFn>(
        dlsym(RTLD_DEFAULT, "insight9_receive_get_video_dev"));

    int bestIndex = std::numeric_limits<int>::max();
    std::string bestPath;
    if (getVideoDev) {
        for (int camId = 0; camId < 3; ++camId) {
            const char* videoDev = getVideoDev(camId);
            if (!videoDev || videoDev[0] == '\0') {
                continue;
            }

            const std::string path(videoDev);
            const auto pos = path.find_last_not_of("0123456789");
            if (pos == std::string::npos || pos + 1 >= path.size()) {
                continue;
            }

            const int index = std::stoi(path.substr(pos + 1));
            if (index < bestIndex) {
                bestIndex = index;
                bestPath = path;
            }
        }
    }
    if (!bestPath.empty()) {
        return bestPath;
    }

    for (const auto& stream : config.streams) {
        if (!stream.device_path.empty()) {
            return stream.device_path;
        }
        if (!stream.device.empty()) {
            return stream.device;
        }
    }

    for (const auto& stream : config.streams) {
        if (stream.vid == 0 || stream.pid == 0) {
            continue;
        }
        const auto devices = viewer::scan_uvc_devices(stream.vid, stream.pid);
        if (!devices.empty()) {
            return devices.front().video_dev;
        }
    }

    return {};
}

} // namespace

namespace viewer {
    const Application::UILayout Application::kUILayout;

    Application::Application() {
        latest_frames_.resize(4);
        frame_ready_.resize(4, false);
    }
    Application::~Application() {
        shutdown();
    }

    bool Application::initialize(const std::string& config_path) {
        config_ = loadConfig(config_path);  // 需完善 loadConfig
        const size_t settingsCount = std::min(config_.streams.size(), kLogicalVideoSettingsCount);
        video_settings_.resize(settingsCount);
        for (size_t i = 0; i < settingsCount; ++i) {
            syncVideoSettingsFromStream(i, config_.streams[i], video_settings_[i]);
        }

        if (insight9_receive_init() != 0) {
            printf("SDK init failed\n");
            return false;
        }

        insight9_receive_register_image_callback(on_image, this);
        insight9_receive_register_imu_callback(on_imu, this);
        insight9_receive_register_vio_callback(on_vio, this);
        if (insight9_receive_start() != 0) {
            addLog("Insight9_receive SDK start failed\n");
            insight9_receive_cleanup();
            return false;
        }

        last_frame_time_ = std::chrono::steady_clock::now();
        last_reconnect_attempt_ = std::chrono::steady_clock::time_point::min();
        addLog("Insight9_receive SDK started successfully\n");

        loadSettings();
        initializeXuController();

        // if (hid_init()) return false;
        tjInstance_ = tjInitDecompress();
        if (!tjInstance_) return false;

        // 初始化 SDL / OpenGL
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
        window_ = SDL_CreateWindow(config_.app_name.c_str(),
                                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                1280, 720,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        gl_context_ = SDL_GL_CreateContext(window_);
        SDL_GL_MakeCurrent(window_, gl_context_);
        SDL_GL_SetSwapInterval(config_.vsync ? 1 : 0);

        // 初始化 ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().FontGlobalScale = 1.5f;
        ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_);
        ImGui_ImplOpenGL3_Init("#version 130");

        // 创建纹理句柄；具体存储在首帧到来时按实际尺寸分配
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (int i = 0; i < 4; ++i) {
            TextureInfo tex;
            glGenTextures(1, &tex.id);
            glBindTexture(GL_TEXTURE_2D, tex.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            tex.width = 0;
            tex.height = 0;
            tex.format = 0;
            textures_.push_back(tex);
            timestamps_.push_back(0);
            last_video_frame_times_.push_back(std::chrono::steady_clock::time_point{});
        }

        running_ = true;
        return true;
    }

    void Application::run() {
        auto interval_imu = std::chrono::milliseconds(1000 / 400);
        auto interval_vio = std::chrono::milliseconds(1000 / 30);
        while (running_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT) running_ = false;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now - last_frame_time_ > std::chrono::seconds(3) &&
                now - last_reconnect_attempt_ > std::chrono::seconds(3)) {
                last_reconnect_attempt_ = now;
                addLog("[Reconnect] No camera frame for 3 seconds, restarting SDK...\n");
                insight9_receive_stop();
                insight9_receive_cleanup();
                if (insight9_receive_init() != 0) {
                    addLog("[Reconnect] SDK init failed\n");
                } else {
                    insight9_receive_register_image_callback(on_image, this);
                    insight9_receive_register_imu_callback(on_imu, this);
                    insight9_receive_register_vio_callback(on_vio, this);
                    if (insight9_receive_start() != 0) {
                        addLog("[Reconnect] SDK start failed\n");
                        insight9_receive_cleanup();
                    } else {
                        addLog("[Reconnect] SDK restarted successfully\n");
                        last_frame_time_ = std::chrono::steady_clock::now();
                        if (!xu_available_) {
                            initializeXuController();
                        }
                    }
                }
            }

            // 开始 ImGui 帧
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            // 菜单栏等
            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Quit")) running_ = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Video Window", NULL, &show_video_window_);
                    ImGui::MenuItem("Sensor Window", NULL, &show_sensor_window_);
                    ImGui::MenuItem("Log Window", NULL, &show_log_window_);
                    ImGui::MenuItem("Camera Setting", NULL, &show_config_);
                    // 可以添加其他窗口的切换项
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Tools")) {
                    if (ImGui::MenuItem("Settings")) show_settings_ = true;
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }

            // 渲染所有 UI
            renderUI();

            // 渲染 ImGui
            ImGui::Render();
            glViewport(0, 0, 1280, 720);
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(window_);
        }
    }

    void Application::shutdown() {
        running_ = false;
        insight9_receive_stop();
        insight9_receive_cleanup();

        for (auto& tex : textures_) {
            if (tex.id) glDeleteTextures(1, &tex.id);
        }

        if (tjInstance_) {
            tjDestroy(tjInstance_);
            tjInstance_ = nullptr;
        }
        if (xu_controller_) {
            xu_controller_->close();
            xu_controller_.reset();
        }
        
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();

        if (gl_context_) {
            SDL_GL_DeleteContext(gl_context_);
            gl_context_ = nullptr;
        }
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        SDL_Quit();
    }

    bool Application::initializeXuController() {
        xu_device_path_ = resolveXuDevicePath(config_);
        xu_available_ = false;
        xu_controller_.reset();

        if (xu_device_path_.empty()) {
            printf("XU device path not resolved from config\n");
            return false;
        }

        auto controller = std::make_unique<UvcExtensionUnit>();
        if (!controller->open(xu_device_path_)) {
            printf("Failed to open XU control on %s\n", xu_device_path_.c_str());
            return false;
        }

        xu_available_ = true;
        xu_controller_ = std::move(controller);
        printf("XU control ready on %s\n", xu_device_path_.c_str());
        return true;
    }

    void Application::onImage(int cam_id, uint8_t* data, size_t size,
                          int width, int height, unsigned int format,
                          uint64_t timestamp, uint64_t right_timestamp) {
        // 注意：此回调在采集线程中执行，需要加锁保护纹理数据和队列
        std::lock_guard<std::mutex> lock(img_mutex_);  // 需要新增 img_mutex_

        if (cam_id == 0) {
            // RGB MJPEG 直接存储
            if (latest_frames_.size() < 1) latest_frames_.resize(4);
            CompressedFrame frame;
            frame.host_rx_ts_ns = viewer::nowNs();
            frame.device_ts_ns = timestamp;
            frame.width = width;
            frame.height = height;
            frame.format = PixelFormat::MJPEG;
            frame.data.assign(data, data + size);
            latest_frames_[0] = std::move(frame);
            frame_ready_[0] = true;
        }
        else if (cam_id == 1) {
            // 合并帧：544x1281 灰度，实际有效高度为 height-1 (1280)，末尾16字节为两个时间戳
            // 拆分为左右两个 544x640 灰度图
            int sub_height = (height - 1) / 2;  // 640
            if (sub_height * 2 != height - 1) {
                fprintf(stderr, "Warning: unexpected combined frame height %d\n", height);
                return;
            }
            size_t sub_size = width * sub_height;  // 544*640 字节
            
            // 左图数据在前半部分
            CompressedFrame left_frame;
            left_frame.host_rx_ts_ns = viewer::nowNs();
            left_frame.device_ts_ns = timestamp;          // 左图时间戳
            left_frame.width = width;
            left_frame.height = sub_height;
            left_frame.format = PixelFormat::GREY;
            left_frame.data.assign(data, data + sub_size);
            
            // 右图数据在后半部分（紧接着左图）
            CompressedFrame right_frame;
            right_frame.host_rx_ts_ns = viewer::nowNs();
            right_frame.device_ts_ns = right_timestamp;   // 右图时间戳
            right_frame.width = width;
            right_frame.height = sub_height;
            right_frame.format = PixelFormat::GREY;
            right_frame.data.assign(data + sub_size, data + sub_size * 2);
            
            latest_frames_[1] = std::move(left_frame);
            latest_frames_[2] = std::move(right_frame);
            frame_ready_[1] = true;
            frame_ready_[2] = true;
        }
        else if (cam_id == 2) {
            // 深度图 Z16，分辨率为 width x height (544x642)，实际有效数据可能少两行（存放时间戳）
            // 假设有效深度数据为 width*(height-2) 字节，末尾 width*2 字节为时间戳等（可忽略）
            int valid_height = height - 2;
            if (valid_height < 0) valid_height = height;
            size_t depth_bytes = width * valid_height * 2;  // 每个像素2字节
            if (size < depth_bytes) {
                fprintf(stderr, "Depth frame too small\n");
                return;
            }
            CompressedFrame depth_frame;
            depth_frame.host_rx_ts_ns = viewer::nowNs();
            depth_frame.device_ts_ns = timestamp;
            depth_frame.width = width;
            depth_frame.height = valid_height;
            depth_frame.format = PixelFormat::Z16;   // 需在 Types.hpp 中添加
            depth_frame.data.assign(data, data + depth_bytes);
            
            latest_frames_[3] = std::move(depth_frame);
            frame_ready_[3] = true;
        }
    }

    void Application::onImu(float ax, float ay, float az,
                            float gx, float gy, float gz,
                            uint64_t timestamp) {
        std::lock_guard<std::mutex> lock(sensor_mutex_);
        latest_imu_.accel[0] = ax;
        latest_imu_.accel[1] = ay;
        latest_imu_.accel[2] = az;
        latest_imu_.gyro[0] = gx;
        latest_imu_.gyro[1] = gy;
        latest_imu_.gyro[2] = gz;
        latest_imu_.device_ts_ns = (uint64_t)timestamp * 1000; // 假设微秒转纳秒
        latest_imu_.host_ts_ns = viewer::nowNs();
        latest_imu_.valid = true;
    }

    void Application::onVio(float px, float py, float pz,
                            float qx, float qy, float qz, float qw,
                            uint64_t timestamp) {
        std::lock_guard<std::mutex> lock(sensor_mutex_);
        latest_vio_.position[0] = px;
        latest_vio_.position[1] = py;
        latest_vio_.position[2] = pz;
        latest_vio_.orientation_xyzw[0] = qx;
        latest_vio_.orientation_xyzw[1] = qy;
        latest_vio_.orientation_xyzw[2] = qz;
        latest_vio_.orientation_xyzw[3] = qw;
        latest_vio_.host_ts_ns = viewer::nowNs();
        latest_vio_.valid = true;
    }

    void Application::depthToRGB(const uint16_t* depth, int width, int height, std::vector<uint8_t>& rgb) {
        rgb.resize(width * height * 3);
        // 找到有效深度范围（忽略0值）
        uint16_t min_depth = 65535, max_depth = 0;
        for (int i = 0; i < width * height; ++i) {
            uint16_t d = depth[i];
            if (d > 0) {
                if (d < min_depth) min_depth = d;
                if (d > max_depth) max_depth = d;
            }
        }
        if (max_depth <= min_depth) max_depth = min_depth + 1;
        
        // 简单的 jet colormap 映射
        for (int i = 0; i < width * height; ++i) {
            uint16_t d = depth[i];
            float t = (d - min_depth) / (float)(max_depth - min_depth);
            // 限制范围
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            
            uint8_t r, g, b;
            // 使用彩虹色：t=0 -> 蓝, t=0.33 -> 青, t=0.66 -> 黄, t=1 -> 红
            if (t < 0.25f) {
                r = 0;
                g = (uint8_t)(t * 4.0f * 255);
                b = 255;
            } else if (t < 0.5f) {
                r = 0;
                g = 255;
                b = (uint8_t)((1.0f - (t - 0.25f) * 4.0f) * 255);
            } else if (t < 0.75f) {
                r = (uint8_t)((t - 0.5f) * 4.0f * 255);
                g = 255;
                b = 0;
            } else {
                r = 255;
                g = (uint8_t)((1.0f - (t - 0.75f) * 4.0f) * 255);
                b = 0;
            }
            rgb[i*3] = r;
            rgb[i*3+1] = g;
            rgb[i*3+2] = b;
        }
    }

    int Application::settingsIndexForDisplay(size_t index) const {
        if (index == 0) {
            return 0;
        }
        if (index == 1 || index == 2) {
            return 1;
        }
        return -1;
    }

    int Application::rotationAngleForDisplay(size_t index) const {
        const int settingsIndex = settingsIndexForDisplay(index);
        if (settingsIndex < 0 || settingsIndex >= static_cast<int>(video_settings_.size())) {
            return 0;
        }

        int normalized = normalizeRotationAngle(video_settings_[settingsIndex].rotation);
        if (normalized == 270) {
            normalized = -90;
        }
        return normalized;
    }

    void Application::updateTexture(const CompressedFrame& frame, size_t index) {
        if (index >= textures_.size()) return;
        auto& info = textures_[index];
        if (info.id == 0) return;

        glBindTexture(GL_TEXTURE_2D, info.id);
        timestamps_[index] = frame.device_ts_ns;

        const int rotation = rotationAngleForDisplay(index);
        const int uploadAngle = (rotation == -90) ? 270 : rotation;

        auto upload = [&](GLenum internalFormat, GLenum dataFormat, const void* pixels,
                          int uploadWidth, int uploadHeight) {
            if (info.width != uploadWidth || info.height != uploadHeight ||
                info.format != internalFormat) {
                glTexImage2D(GL_TEXTURE_2D, 0, internalFormat,
                             uploadWidth, uploadHeight, 0,
                             dataFormat, GL_UNSIGNED_BYTE, pixels);
                info.width = uploadWidth;
                info.height = uploadHeight;
                info.format = internalFormat;
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                uploadWidth, uploadHeight,
                                dataFormat, GL_UNSIGNED_BYTE, pixels);
            }
        };

        if (frame.format == PixelFormat::GREY) {
            if (uploadAngle == 0) {
                upload(GL_LUMINANCE, GL_LUMINANCE, frame.data.data(), frame.width, frame.height);
            } else {
                rotateGrey(frame.data.data(), frame.width, frame.height, rotated_grey_scratch_, uploadAngle);
                const int rotatedWidth = (uploadAngle == 90 || uploadAngle == 270) ? frame.height : frame.width;
                const int rotatedHeight = (uploadAngle == 90 || uploadAngle == 270) ? frame.width : frame.height;
                upload(GL_LUMINANCE, GL_LUMINANCE, rotated_grey_scratch_.data(), rotatedWidth, rotatedHeight);
            }
        }
        else if (frame.format == PixelFormat::MJPEG) {
            rgb_scratch_.resize(frame.width * frame.height * 3);
            if (tjDecompress2(tjInstance_, frame.data.data(), frame.data.size(),
                            rgb_scratch_.data(), frame.width, 0, frame.height, TJPF_RGB, 0) < 0) {
                const char* err = tjGetErrorStr2(tjInstance_);
                fprintf(stderr, "JPEG decompress error: %s\n", err);
                return;
            }
            if (uploadAngle == 0) {
                upload(GL_RGB, GL_RGB, rgb_scratch_.data(), frame.width, frame.height);
            } else {
                rotateRGB(rgb_scratch_.data(), frame.width, frame.height, rotated_rgb_scratch_, uploadAngle);
                const int rotatedWidth = (uploadAngle == 90 || uploadAngle == 270) ? frame.height : frame.width;
                const int rotatedHeight = (uploadAngle == 90 || uploadAngle == 270) ? frame.width : frame.height;
                upload(GL_RGB, GL_RGB, rotated_rgb_scratch_.data(), rotatedWidth, rotatedHeight);
            }
        }
        else if (frame.format == PixelFormat::Z16) {
            const uint16_t* depth = reinterpret_cast<const uint16_t*>(frame.data.data());
            depthToRGB(depth, frame.width, frame.height, rgb_scratch_);
            upload(GL_RGB, GL_RGB, rgb_scratch_.data(), frame.width, frame.height);
        }
    }

    void Application::renderCameraStatus(bool hasRecentFrame, const char* cameraLabel) {
        if (hasRecentFrame) return;   // 有帧，不需要显示覆盖

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
        ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
        ImVec2 minPos(winPos.x + contentMin.x, winPos.y + contentMin.y);
        ImVec2 maxPos(winPos.x + contentMax.x, winPos.y + contentMax.y);

        // 计算文本尺寸和显示区域
        const char* line1 = cameraLabel;
        const char* line2 = "Camera no connection or";
        const char* line3 = "no recent frames received!";

        ImVec2 s1 = ImGui::CalcTextSize(line1);
        ImVec2 s2 = ImGui::CalcTextSize(line2);
        ImVec2 s3 = ImGui::CalcTextSize(line3);
        float textWidth = std::max(s1.x, std::max(s2.x, s3.x));
        float textHeight = s1.y + s2.y + s3.y + 18.0f;
        ImVec2 boxSize(std::min(textWidth + 32.0f, std::max(160.0f, maxPos.x - minPos.x - 20.0f)),
                    textHeight + 24.0f);
        ImVec2 boxPos(minPos.x + (maxPos.x - minPos.x - boxSize.x) * 0.5f,
                    minPos.y + (maxPos.y - minPos.y - boxSize.y) * 0.5f);

        drawList->AddRectFilled(boxPos, ImVec2(boxPos.x + boxSize.x, boxPos.y + boxSize.y),
                                IM_COL32(0, 0, 0, 190), 6.0f);
        drawList->AddRect(boxPos, ImVec2(boxPos.x + boxSize.x, boxPos.y + boxSize.y),
                        IM_COL32(255, 190, 80, 230), 6.0f);

        ImVec2 textPos(boxPos.x + 16.0f, boxPos.y + 12.0f);
        drawList->AddText(textPos, IM_COL32(255, 220, 160, 255), line1);
        textPos.y += s1.y + 6.0f;
        drawList->AddText(textPos, IM_COL32(235, 235, 235, 255), line2);
        textPos.y += s2.y + 6.0f;
        drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), line3);
    }

    void Application::renderUI() {
        flushPendingLogs();
        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        if (screenSize.x < 1.0f || screenSize.y < 1.0f) {
            screenSize = ImVec2(1280.0f, 720.0f);
        }
        const float baseWidth = 1280.0f;
        const float baseHeight = 720.0f;
        const float scaleX = screenSize.x / baseWidth;
        const float scaleY = screenSize.y / baseHeight;

        // 视频窗口
        if (show_video_window_) {
            const char* cam_names[] = {"RGB Camera", "Left Grey Camera", "Right Grey Camera"};
            // 窗口位置（保持原有布局）
            ImVec2 positions[] = {ImVec2(kUILayout.rgb.x * scaleX, kUILayout.rgb.y * scaleY), ImVec2(kUILayout.leftGrey.x * scaleX, kUILayout.leftGrey.y * scaleY), ImVec2(kUILayout.rightGrey.x * scaleX, kUILayout.rightGrey.y * scaleY)};
            ImVec2 size[] = {ImVec2(kUILayout.rgb.w * scaleX, kUILayout.rgb.h * scaleY), ImVec2(kUILayout.leftGrey.w * scaleX, kUILayout.leftGrey.h * scaleY), ImVec2(kUILayout.rightGrey.w * scaleX, kUILayout.rightGrey.h * scaleY)};

            for (int i = 0; i < 3; ++i) {
                // 在锁内只把最新帧 move 出来，解码/GL 上传放到锁外，避免阻塞 SDK 回调线程
                CompressedFrame local_frame;
                bool has_frame = false;
                {
                    std::lock_guard<std::mutex> lock(img_mutex_);
                    if (frame_ready_[i]) {
                        local_frame = std::move(latest_frames_[i]);
                        frame_ready_[i] = false;
                        has_frame = true;
                    }
                }
                if (has_frame) {
                    updateTexture(local_frame, i);
                    if (i < last_video_frame_times_.size()) {
                        last_video_frame_times_[i] = std::chrono::steady_clock::now();
                    }
                }

                bool hasRecentFrame = (i < last_video_frame_times_.size() &&
                                   last_video_frame_times_[i].time_since_epoch().count() != 0 &&
                                   (std::chrono::steady_clock::now() - last_video_frame_times_[i]) <= std::chrono::seconds(3));

                ImGui::SetNextWindowPos(positions[i], lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(size[i], lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
                ImGui::Begin(cam_names[i]);
                GLuint texId = textures_[i].id;
                if (hasRecentFrame && texId != 0 && textures_[i].width > 0 && textures_[i].height > 0) {
                    // 按比例显示图像（与原来相同）
                    ImVec2 winSize = ImGui::GetContentRegionAvail();
                    float aspect = (float)textures_[i].width / textures_[i].height;
                    ImVec2 imageSize;
                    if (winSize.x / aspect < winSize.y) {
                        imageSize.x = winSize.x;
                        imageSize.y = winSize.x / aspect;
                    } else {
                        imageSize.y = winSize.y;
                        imageSize.x = winSize.y * aspect;
                    }
                    ImVec2 imagePos = ImGui::GetCursorScreenPos();
                    ImGui::Image((void*)(intptr_t)texId, imageSize);

                    if (timestamps_[i] != 0) {
                        ImDrawList* draw_list = ImGui::GetWindowDrawList();
                        char ts_str[64];
                        snprintf(ts_str, sizeof(ts_str), "T: %" PRIu64, timestamps_[i]);
                        ImVec2 text_size = ImGui::CalcTextSize(ts_str);
                        ImVec2 text_pos = ImVec2(imagePos.x + imageSize.x - text_size.x - 10, imagePos.y + 10);
                        draw_list->AddRectFilled(text_pos, ImVec2(text_pos.x + text_size.x + 6, text_pos.y + text_size.y + 6),
                                                IM_COL32(0, 0, 0, 128));
                        draw_list->AddText(ImVec2(text_pos.x + 3, text_pos.y + 3), IM_COL32(255, 255, 255, 255), ts_str);
                    }
                } else {
                    renderCameraStatus(hasRecentFrame, cam_names[i]);
                }
                ImGui::End();
            }
        }

        // 深度图窗口
        if (show_depth_window_) {
            ImGui::SetNextWindowPos(ImVec2(kUILayout.depth.x * scaleX, kUILayout.depth.y * scaleY), lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(kUILayout.depth.w * scaleX, kUILayout.depth.h * scaleY), lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
            ImGui::Begin("Depth Map", &show_depth_window_);
            {
                CompressedFrame local_frame;
                bool has_frame = false;
                {
                    std::lock_guard<std::mutex> lock(img_mutex_);
                    if (frame_ready_[3]) {
                        local_frame = std::move(latest_frames_[3]);
                        frame_ready_[3] = false;
                        has_frame = true;
                    }
                }
                if (has_frame) {
                    updateTexture(local_frame, 3);
                    if (3 < last_video_frame_times_.size()) {
                        last_video_frame_times_[3] = std::chrono::steady_clock::now();
                    }
                }
            }
            bool hasRecentFrame = (3 < last_video_frame_times_.size() &&
                                   last_video_frame_times_[3].time_since_epoch().count() != 0 &&
                                   (std::chrono::steady_clock::now() - last_video_frame_times_[3]) <= std::chrono::seconds(3));
            GLuint texId = textures_[3].id;
            if (hasRecentFrame && texId != 0 && textures_[3].width > 0 && textures_[3].height > 0) {
                ImGui::Image((void*)(intptr_t)texId, ImVec2(textures_[3].width, textures_[3].height));
            } else {
                renderCameraStatus(hasRecentFrame, "Depth Camera");
            }
            ImGui::End();
        }

        // 传感器窗口
        if (show_sensor_window_) {
            ImGui::SetNextWindowPos(ImVec2(kUILayout.sensor.x * scaleX, kUILayout.sensor.y * scaleY), lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(kUILayout.sensor.w * scaleX, kUILayout.sensor.h * scaleY), lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
            ImGui::Begin("Sensor");
            ImGui::Text("IMU: accel=(%f,%f,%f) gyro=(%f,%f,%f)",
                        latest_imu_.accel[0], latest_imu_.accel[1], latest_imu_.accel[2],
                        latest_imu_.gyro[0], latest_imu_.gyro[1], latest_imu_.gyro[2]);
            ImGui::Text("VIO: pos=(%f,%f,%f) ori=(%f %f %f %f)",
                        latest_vio_.position[0], latest_vio_.position[1], latest_vio_.position[2],
                        latest_vio_.orientation_xyzw[0], latest_vio_.orientation_xyzw[1], latest_vio_.orientation_xyzw[2], latest_vio_.orientation_xyzw[3]);
            ImGui::End();
        }

        // 渲染设置窗口
        renderConfigWindow(scaleX, scaleY);

        if (show_log_window_) {
            renderLogWindow(scaleX, scaleY);
        }

        // 设置窗口
        if (show_settings_) {
            ImGui::SetNextWindowPos(ImVec2(0, 25), lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(1850, 1110), lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);

            ImGui::Begin("Settings", &show_settings_);  // 第二个参数为关闭窗口时自动置 false
            ImGui::Text("Adjust settings...");

            ImGui::Checkbox("Lock Window Position/Size", &lock_windows_);
            // if (ImGui::Button("Save Screenshot")) {
            //     // 在这里调用截图函数，例如 saveScreenshot();
            //     // saveScreenshot();
            //     printf("嘻嘻~\n");
            // }
            // ImGui::SameLine();  // 使下一个按钮在同一行
            if (ImGui::Button("Close")) {
                show_settings_ = false;
            }

            ImGui::End();
        }

        ShowToast();
    }

    void Application::renderConfigWindow(float scaleX, float scaleY) {
        if (!show_config_) return;

        ImGui::SetNextWindowSize(ImVec2(kUILayout.config.w * scaleX, kUILayout.config.h * scaleY), lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(kUILayout.config.x * scaleX, kUILayout.config.y * scaleY), lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

        if (ImGui::Begin("Camera Settings", &show_config_, flags)) {
            // 辅助函数：Push 三种 Header 颜色
            auto pushHeaderColor = [](const ImVec4& color) {
                ImGui::PushStyleColor(ImGuiCol_Header, color);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(color.x * 1.2f, color.y * 1.2f, color.z * 1.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(color.x * 0.8f, color.y * 0.8f, color.z * 0.8f, 1.0f));
            };
            auto popHeaderColor = []() { ImGui::PopStyleColor(3); };

            // ========== Video Settings（蓝色） ==========
            pushHeaderColor(ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
            if (ImGui::CollapsingHeader("Video Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Camera Selection");
                const char* cameras[] = { "Camera RGB (MJPEG)", "Camera BW (Grey)" };
                for (int i = 0; i < 2; ++i) {
                    if (ImGui::RadioButton(cameras[i], current_camera_index_ == i)) {
                        current_camera_index_ = i;
                    }
                }
                ImGui::Separator();

                if (current_camera_index_ >= 0 && current_camera_index_ < static_cast<int>(video_settings_.size())) {
                    VideoSettings& vs = video_settings_[current_camera_index_];

                    // // 分辨率
                    // ImGui::Text("Resolution");
                    // const char* resolutions_rgb[] = { "3840x2160", "1088x1920", "1280x720", "640x480" };
                    // const char* resolutions_bl[] = { "1088x1280", "544x640" };
                    // if (current_camera_index_ == 0) {
                    //     for (int i = 0; i < 4; ++i) {
                    //         if (ImGui::RadioButton(resolutions_rgb[i], vs.resolution_index == i)) {
                    //             vs.resolution_index = i;
                    //         }
                    //     }
                    // } else {
                    //     for (int i = 0; i < 2; ++i) {
                    //         if (ImGui::RadioButton(resolutions_bl[i], vs.resolution_index == i)) {
                    //             vs.resolution_index = i;
                    //         }
                    //     }
                    // }
                    // ImGui::Separator();

                    // // FPS
                    // ImGui::Text("FPS");
                    // const char* fps[] = { "90 fps", "60 fps", "30 fps", "20 fps", "15 fps", "10 fps" };
                    // for (int i = 0; i < 6; ++i) {
                    //     if (ImGui::RadioButton(fps[i], vs.fps == i)) {
                    //         vs.fps = i;
                    //     }
                    // }

                    // Controls（蓝色）
                    pushHeaderColor(ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
                    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::SliderFloat("Exposure Time", &vs.exposure_time, 0.1f, 10000.0f);
                        ImGui::SliderFloat("Exposure Gain", &vs.exposure_gain, 1.0f, 10000.0f);
                        ImGui::Checkbox("Backlight Compensation", &vs.backlight_comp);
                        ImGui::SliderFloat("Brightness", &vs.brightness, -64.0f, 64.0f);
                        ImGui::SliderFloat("Contrast", &vs.contrast, 0.0f, 100.0f);
                        ImGui::SliderFloat("Gamma_Dark", &vs.gamma_dark, 100.0f, 500.0f);
                        ImGui::SliderFloat("hue", &vs.hue, -180.0f, 180.0f);
                        ImGui::SliderFloat("Saturation", &vs.saturation, 0.0f, 100.0f);
                        ImGui::SliderFloat("Sharpness", &vs.sharpness, 0.0f, 100.0f);
                        ImGui::Checkbox("Enable Auto White Balance", &vs.auto_white_balance);
                        ImGui::SliderFloat("White_Balance", &vs.white_balance, 2800.0f, 6500.0f);
                    }
                    popHeaderColor();

                    // Post-Processing（蓝色）
                    pushHeaderColor(ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
                    if (ImGui::CollapsingHeader("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::SliderFloat("Decimation Filter", &vs.decimation, 1.0f, 8.0f);
                        ImGui::SliderFloat("Rotation Filter", &vs.rotation, -90.0f, 180.0f);
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            // 拖拽结束后，将角度标准化并更新显示
                            int normalized = normalizeRotationAngle(vs.rotation);
                            if (normalized == 270) normalized = -90;  // 将270度显示为-90度
                            vs.rotation = static_cast<float>(normalized);
                        }
                    }
                    popHeaderColor();
                }
            }
            popHeaderColor();

            // // ========== Sensor Settings（紫色） ==========
            // pushHeaderColor(ImVec4(0.7f, 0.4f, 0.8f, 1.0f));
            // if (ImGui::CollapsingHeader("Sensor Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            //     ImGui::Checkbox("Enable IMU", &sensor_settings_.enable_imu);
            //     ImGui::Checkbox("Enable VIO", &sensor_settings_.enable_vio);
            //     if (sensor_settings_.enable_imu) {
            //         ImGui::Indent();
            //         ImGui::SliderInt("IMU Rate (Hz)", &sensor_settings_.imu_rate, 50, 1000);
            //         ImGui::Unindent();
            //     }
            //     if (sensor_settings_.enable_vio) {
            //         ImGui::Indent();
            //         ImGui::SliderInt("VIO Rate (Hz)", &sensor_settings_.vio_rate, 10, 200);
            //         ImGui::Unindent();
            //     }
            //     ImGui::Checkbox("Raw Mode (unprocessed)", &sensor_settings_.raw_mode);
            // }
            // popHeaderColor();

            // // ========== Display Settings（浅蓝色） ==========
            // pushHeaderColor(ImVec4(0.4f, 0.7f, 0.9f, 1.0f));
            // if (ImGui::CollapsingHeader("Display Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            //     ImGui::Checkbox("Show Video Windows", &show_video_window_);
            //     ImGui::Checkbox("Show Sensor Window", &show_sensor_window_);
            //     ImGui::Checkbox("Show Log Window", &show_log_window_);
            //     ImGui::Checkbox("Auto-scroll Log", &auto_scroll_log_);
            // }
            // popHeaderColor();

            // 按钮
            if (ImGui::Button("Apply", ImVec2(120, 0))) {
                saveSettings();
                applySettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                show_config_ = false;
            }
        }
        ImGui::End();
    }

    void Application::renderLogWindow(float scaleX, float scaleY) {
        ImGui::SetNextWindowSize(ImVec2(kUILayout.log.w * scaleX, kUILayout.log.h * scaleY), lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(kUILayout.log.x * scaleX, kUILayout.log.y * scaleY), lock_windows_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Backend Log", &show_log_window_)) {
            ImGui::End();
            return;
        }

        if (ImGui::Button("Clear")) {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            g_log_lines.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &auto_scroll_log_);
        ImGui::Separator();

        ImGui::BeginChild("LogScrollArea", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& line : g_log_lines) {
            ImGui::TextUnformatted(line.c_str());
        }
        if (auto_scroll_log_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void Application::applySettings() {
        if (!video_settings_.empty() && !config_.streams.empty()) {
            syncStreamFromVideoSettings(0, video_settings_[0], config_.streams[0]);
        }
        if (video_settings_.size() > 1 && config_.streams.size() > 1) {
            syncStreamFromVideoSettings(1, video_settings_[1], config_.streams[1]);
        }
        if (video_settings_.size() > 1 && config_.streams.size() > 2) {
            syncStreamFromVideoSettings(1, video_settings_[1], config_.streams[2]);
        }

        bool appliedToDevice = false;
        if (current_camera_index_ >= 0 && current_camera_index_ < (int)video_settings_.size()) {
            VideoSettings& vs = video_settings_[current_camera_index_];
            VideoStreamConfig& stream = config_.streams[current_camera_index_ == 0 ? 0 : 1];
            if (xu_controller_ || initializeXuController()) {
                const camera_params params = toCameraParams(static_cast<uint8_t>(current_camera_index_), vs);
                appliedToDevice = xu_controller_ &&
                    xu_controller_->writeCameraParams(static_cast<uint8_t>(current_camera_index_), params);
                if (appliedToDevice) {
                    printParams(params);
                }
            }

            printf("Applied settings for camera %d: resolution_index=%d, fps_index=%d, fps=%d, brightness=%.2f, xu=%s\n",
                current_camera_index_, vs.resolution_index, vs.fps, stream.fps, vs.brightness,
                appliedToDevice ? "ok" : "not_applied");
        }
        printf("IMU=%d, VIO=%d, IMU rate=%d, VIO rate=%d\n",
            sensor_settings_.enable_imu, sensor_settings_.enable_vio,
            sensor_settings_.imu_rate, sensor_settings_.vio_rate);

        toast_message = appliedToDevice ? "Setting success!" : "Setting saved, XU not applied";
        toast_start_time = ImGui::GetTime();
        show_toast = true;
    }

    void Application::saveSettings() {
        nlohmann::json j;
        // 仅持久化逻辑上的两组设置：RGB 和 BW
        for (size_t i = 0; i < std::min(video_settings_.size(), kLogicalVideoSettingsCount); ++i) {
            const VideoSettings& vs = video_settings_[i];
            j["video_cameras"][i]["resolution_index"] = vs.resolution_index;
            j["video_cameras"][i]["fps"] = vs.fps;
            j["video_cameras"][i]["exposure_time"] = vs.exposure_time;
            j["video_cameras"][i]["exposure_gain"] = vs.exposure_gain;
            j["video_cameras"][i]["backlight_comp"] = vs.backlight_comp;
            j["video_cameras"][i]["brightness"] = vs.brightness;
            j["video_cameras"][i]["contrast"] = vs.contrast;
            j["video_cameras"][i]["gamma_dark"] = vs.gamma_dark;
            j["video_cameras"][i]["hue"] = vs.hue;
            j["video_cameras"][i]["saturation"] = vs.saturation;
            j["video_cameras"][i]["sharpness"] = vs.sharpness;
            j["video_cameras"][i]["auto_white_balance"] = vs.auto_white_balance;
            j["video_cameras"][i]["white_balance"] = vs.white_balance;
            j["video_cameras"][i]["decimation"] = vs.decimation;
            j["video_cameras"][i]["rotation"] = vs.rotation;
        }

        j["sensor"]["enable_imu"] = sensor_settings_.enable_imu;
        j["sensor"]["enable_vio"] = sensor_settings_.enable_vio;
        j["sensor"]["imu_rate"] = sensor_settings_.imu_rate;
        j["sensor"]["vio_rate"] = sensor_settings_.vio_rate;
        j["sensor"]["raw_mode"] = sensor_settings_.raw_mode;

        j["display"]["show_video_window"] = show_video_window_;
        j["display"]["show_sensor_window"] = show_sensor_window_;
        j["display"]["show_log_window"] = show_log_window_;
        j["display"]["auto_scroll_log"] = auto_scroll_log_;

        std::ofstream ofs("settings.json");
        if (ofs.is_open()) {
            ofs << j.dump(4);
            ofs.close();
            printf("Settings saved to settings.json\n");
        } else {
            printf("Failed to save settings\n");
        }
    }

    void Application::loadSettings() {
        std::ifstream ifs("settings.json");
        if (!ifs.is_open()) {
            printf("Settings file not found, using defaults\n");
            return;
        }
        nlohmann::json j;
        ifs >> j;

        // 加载每个摄像头的设置，并同步到 config_.streams
        if (j.contains("video_cameras") && j["video_cameras"].is_array()) {
            size_t count = std::min(video_settings_.size(),
                                    std::min(j["video_cameras"].size(), kLogicalVideoSettingsCount));
            for (size_t i = 0; i < count; ++i) {
                const auto& camera = j["video_cameras"][i];
                VideoSettings& vs = video_settings_[i];
                vs.resolution_index = readJsonInt(camera, "resolution_index", 0);
                vs.fps = readJsonInt(camera, "fps", fpsValueToIndex(config_.streams[i].fps));
                vs.exposure_time = readJsonFloat(camera, "exposure_time", config_.streams[i].exposure_time);
                vs.exposure_gain = readJsonFloat(camera, "exposure_gain", config_.streams[i].exposure_gain);
                vs.backlight_comp = readJsonBool(camera, "backlight_comp", config_.streams[i].backlight_comp);
                vs.brightness = readJsonFloat(camera, "brightness", 0.0f);
                vs.contrast = readJsonFloat(camera, "contrast", config_.streams[i].contrast);
                vs.gamma_dark = readJsonFloat(camera, "gamma_dark", config_.streams[i].gamma_dark);
                vs.hue = readJsonFloat(camera, "hue", config_.streams[i].hue);
                vs.saturation = readJsonFloat(camera, "saturation", config_.streams[i].saturation);
                vs.sharpness = readJsonFloat(camera, "sharpness", config_.streams[i].sharpness);
                vs.auto_white_balance = readJsonBool(camera, "auto_white_balance", config_.streams[i].auto_white_balance);
                vs.white_balance = readJsonFloat(camera, "white_balance", config_.streams[i].white_balance);
                vs.decimation = readJsonFloat(camera, "decimation", config_.streams[i].decimation);
                vs.rotation = readJsonFloat(camera, "rotation", config_.streams[i].rotation);
            }
        }

        if (!video_settings_.empty() && !config_.streams.empty()) {
            syncStreamFromVideoSettings(0, video_settings_[0], config_.streams[0]);
        }
        if (video_settings_.size() > 1 && config_.streams.size() > 1) {
            syncStreamFromVideoSettings(1, video_settings_[1], config_.streams[1]);
        }
        if (video_settings_.size() > 1 && config_.streams.size() > 2) {
            syncStreamFromVideoSettings(1, video_settings_[1], config_.streams[2]);
        }

        current_camera_index_ = 0;

        const nlohmann::json sensor = (j.contains("sensor") && j["sensor"].is_object()) ? j["sensor"] : nlohmann::json::object();
        sensor_settings_.enable_imu = readJsonBool(sensor, "enable_imu", true);
        sensor_settings_.enable_vio = readJsonBool(sensor, "enable_vio", true);
        sensor_settings_.imu_rate = readJsonInt(sensor, "imu_rate", 400);
        sensor_settings_.vio_rate = readJsonInt(sensor, "vio_rate", 30);
        sensor_settings_.raw_mode = readJsonBool(sensor, "raw_mode", false);

        const nlohmann::json display = (j.contains("display") && j["display"].is_object()) ? j["display"] : nlohmann::json::object();
        show_video_window_ = readJsonBool(display, "show_video_window", true);
        show_sensor_window_ = readJsonBool(display, "show_sensor_window", true);
        show_log_window_ = readJsonBool(display, "show_log_window", false);
        auto_scroll_log_ = readJsonBool(display, "auto_scroll_log", true);

        printf("Settings loaded from settings.json\n");
    }

    void Application::ShowToast() {
        if (!show_toast) return;
        float current_time = ImGui::GetTime();
        if (current_time - toast_start_time >= 2.0f) {
            show_toast = false;
            return;
        }

        // 计算文本尺寸（使用默认字体）
        ImVec2 text_size = ImGui::CalcTextSize(toast_message.c_str());

        // 增加背景内边距（让背景更大）
        float padding_x = 20.0f;
        float padding_y = 12.0f;
        ImVec2 bg_size(text_size.x + padding_x * 2, text_size.y + padding_y * 2);

        // 屏幕底部居中，向上偏移更多
        ImVec2 screen_size = ImGui::GetIO().DisplaySize;
        ImVec2 pos(screen_size.x * 0.5f - bg_size.x * 0.3f, screen_size.y - 80.0f - bg_size.y);

        // 绘制圆角背景（更圆润）
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        draw_list->AddRectFilled(pos, ImVec2(pos.x + bg_size.x, pos.y + bg_size.y),
                                IM_COL32(0, 0, 0, 128), 12.0f);

        // 绘制文字（居中）
        ImVec2 text_pos(pos.x + padding_x, pos.y + padding_y);
        draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), toast_message.c_str());
    }
} // namespace viewer
