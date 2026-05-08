#pragma once
#include "viewer/video/IFrameSource.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <linux/videodev2.h>

namespace viewer {

struct V4l2VideoConfig {
    std::string device;      // 如 "/dev/video0"
    int width = 1280;
    int height = 720;
    int fps = 30;
    PixelFormat format = PixelFormat::MJPEG;  // 或根据 v4l2_fourcc 映射
};

class V4l2VideoSource : public IFrameSource {
public:
    explicit V4l2VideoSource(const V4l2VideoConfig& cfg);
    ~V4l2VideoSource();

    bool start() override;
    void stop() override;
    bool poll(CompressedFrame& frame) override;
    std::string name() const override;

private:
    void captureLoop();  // 采集线程函数

    V4l2VideoConfig cfg_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    int fd_ = -1;
    struct Buffer {
        void* start;
        size_t length;
    };
    std::vector<Buffer> buffers_;

    std::mutex frameMutex_;
    std::queue<CompressedFrame> frameQueue_;  // 从采集线程传递到主线程
    uint64_t lastTimestamp_;  // 用于时间戳计算（可选）

    bool initDevice();
    void cleanupDevice();
};

} // namespace viewer