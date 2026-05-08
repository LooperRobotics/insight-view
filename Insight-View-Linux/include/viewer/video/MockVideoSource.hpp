#pragma once
#include <atomic>
#include <string>
#include "viewer/video/IFrameSource.hpp"

namespace viewer {
struct MockVideoConfig {
    std::string id = "cam0";
    int width = 1280;
    int height = 720;
    int fps = 30;
    PixelFormat format = PixelFormat::MJPEG;
};

class MockVideoSource : public IFrameSource {
public:
    explicit MockVideoSource(MockVideoConfig cfg);
    bool start() override;
    void stop() override;
    bool poll(CompressedFrame& frame) override;
    std::string name() const override;
private:
    MockVideoConfig cfg_;
    std::atomic<bool> running_{false};
    uint32_t seq_ = 0;
    uint64_t last_frame_ts_ns_ = 0;
};
} // namespace viewer
