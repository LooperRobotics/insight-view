#include "viewer/video/V4l2VideoSource.hpp"
#include "viewer/core/Time.hpp"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <thread>
#include <chrono>

namespace viewer {
    V4l2VideoSource::V4l2VideoSource(const V4l2VideoConfig& cfg) : cfg_(cfg), fd_(-1) {}

    V4l2VideoSource::~V4l2VideoSource() {
        stop();
    }

    std::string V4l2VideoSource::name() const {
        return "V4l2:" + cfg_.device;
    }

    bool V4l2VideoSource::start() {
        if (running_) return true;
        running_ = true;
        worker_ = std::thread(&V4l2VideoSource::captureLoop, this);
        return true;
    }

    void V4l2VideoSource::stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
        cleanupDevice();
    }

    bool V4l2VideoSource::initDevice() {
        fd_ = open(cfg_.device.c_str(), O_RDWR);
        if (fd_ < 0) return false;

        // 设置格式
        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = cfg_.width;
        fmt.fmt.pix.height = cfg_.height;
        fmt.fmt.pix.pixelformat = (cfg_.format == PixelFormat::MJPEG) ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_GREY;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) { close(fd_); return false; }

        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) == 0) {
            printf("[%s] Format set: %c%c%c%c %dx%d\n",
                cfg_.device.c_str(),
                (fmt.fmt.pix.pixelformat >> 0) & 0xff,
                (fmt.fmt.pix.pixelformat >> 8) & 0xff,
                (fmt.fmt.pix.pixelformat >> 16) & 0xff,
                (fmt.fmt.pix.pixelformat >> 24) & 0xff,
                fmt.fmt.pix.width, fmt.fmt.pix.height);
        }
        // 设置帧率
        struct v4l2_streamparm parm;
        memset(&parm, 0, sizeof(parm));
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = cfg_.fps;
        ioctl(fd_, VIDIOC_S_PARM, &parm);  // 忽略失败

        // 请求缓冲区
        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) { close(fd_); return false; }

        buffers_.resize(req.count);
        for (int i = 0; i < req.count; ++i) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) { close(fd_); return false; }
            buffers_[i].length = buf.length;
            buffers_[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);
            if (buffers_[i].start == MAP_FAILED) { close(fd_); return false; }
            // 入队
            if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) { close(fd_); return false; }
        }

        // 启动流
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) { close(fd_); return false; }
        return true;
    }

    void V4l2VideoSource::captureLoop() {
        while (running_) {
            if (fd_ < 0) {
                if (!initDevice()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }

            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd_, &fds);
            struct timeval tv = {1, 0};
            int ret = select(fd_+1, &fds, NULL, NULL, &tv);
            if (ret <= 0) continue;

            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
                if (errno == ENODEV) {
                    cleanupDevice();  // 关闭 fd，释放 buffers
                }
                continue;
            }

            const uint8_t* data = static_cast<uint8_t*>(buffers_[buf.index].start);
            size_t dataSize = buf.bytesused;
            uint64_t ts = 0;
            if (dataSize >= 12 && memcmp(data, "TS__", 4) == 0) {
                memcpy(&ts, data + 4, 8);
            }

            CompressedFrame frame;
            frame.host_rx_ts_ns = nowNs();   // 使用你的 Time.hpp
            frame.device_ts_ns = ts;           // 如果有硬件时间戳可提取
            frame.sequence = 0;                // 可维护计数器
            frame.format = cfg_.format;
            frame.width = cfg_.width;
            frame.height = cfg_.height;
            frame.data.assign(static_cast<uint8_t*>(buffers_[buf.index].start),
                            static_cast<uint8_t*>(buffers_[buf.index].start) + buf.bytesused);

            if (cfg_.format == PixelFormat::GREY) {
                if (dataSize != static_cast<size_t>(cfg_.width * cfg_.height)) {
                    printf("[%s] Warning: GREY frame size mismatch: expected %d, got %zu\n",
                        cfg_.device.c_str(), cfg_.width * cfg_.height, dataSize);
                }
            }

            // 入队
            if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
                // 错误处理
            }

            std::lock_guard<std::mutex> lock(frameMutex_);
            if (frameQueue_.size() > 1) frameQueue_.pop();  // 只保留最新一帧
            frameQueue_.push(std::move(frame));
        }
    }

    bool V4l2VideoSource::poll(CompressedFrame& frame) {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if (frameQueue_.empty()) return false;
        frame = std::move(frameQueue_.front());
        frameQueue_.pop();
        return true;
    }

    void V4l2VideoSource::cleanupDevice() {
        if (fd_ >= 0) {
            // 停止流（可选）
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(fd_, VIDIOC_STREAMOFF, &type);
            close(fd_);
            fd_ = -1;
        }
        for (auto& buf : buffers_) {
            if (buf.start && buf.start != MAP_FAILED) {
                munmap(buf.start, buf.length);
            }
        }
        buffers_.clear();
    }
}