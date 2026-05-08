#pragma once
#include <string>
#include "viewer/sensor/ImuSample.hpp"
#include "viewer/sensor/VioSample.hpp"
namespace viewer {
class IHidSensor {
public:
    virtual ~IHidSensor() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool pollImu(ImuSample& sample) = 0;
    virtual bool pollVio(VioSample& sample) = 0;
    virtual std::string name() const = 0;
};
} // namespace viewer
