#pragma once
#include <string>
#include "viewer/video/Frame.hpp"

namespace viewer {
class IFrameSource {
public:
    virtual ~IFrameSource() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool poll(CompressedFrame& frame) = 0;
    virtual std::string name() const = 0;
};
} // namespace viewer
