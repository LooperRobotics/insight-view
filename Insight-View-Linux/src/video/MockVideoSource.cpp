#include "viewer/video/MockVideoSource.hpp"
#include "viewer/core/Time.hpp"
#include <chrono>
#include <thread>

namespace viewer {
    MockVideoSource::MockVideoSource(MockVideoConfig cfg) : cfg_(std::move(cfg)) {}

    bool MockVideoSource::start() {
        running_ = true;
        last_frame_ts_ns_ = nowNs();
        seq_ = 0;
        return true;
    }
    
    void MockVideoSource::stop() {
        running_ = false;
    }
    
    std::string MockVideoSource::name() const {
        return "MockVideoSource:" + cfg_.id;
    }
    
    bool MockVideoSource::poll(CompressedFrame& frame) {
        if (!running_) return false;
        const uint64_t interval_ns = 1000000000ull / static_cast<uint64_t>(cfg_.fps);
        const uint64_t now = nowNs();
        if (now - last_frame_ts_ns_ < interval_ns) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); return false; }
        last_frame_ts_ns_ = now;
        frame.host_rx_ts_ns = now;
        frame.device_ts_ns = now - 2000000ull;
        frame.sequence = seq_++;
        frame.format = cfg_.format;
        frame.width = cfg_.width;
        frame.height = cfg_.height;
        frame.data.resize(1024, static_cast<uint8_t>(frame.sequence & 0xFF));
        return true;
    }
} // namespace viewer
