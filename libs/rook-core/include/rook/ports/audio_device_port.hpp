#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rook::ports {

struct DeviceInfo {
    std::string id;
    std::string name;
    std::string backend;
    bool is_default = false;
};

using AudioCaptureCallback = std::function<void(const int16_t* pcm, std::size_t frame_count)>;

class AudioDevicePort {
public:
    virtual ~AudioDevicePort() = default;

    virtual std::vector<DeviceInfo> enumerateInputs() const = 0;
    virtual std::vector<DeviceInfo> enumerateOutputs() const = 0;

    virtual bool startCapture(std::string_view device_id,
                              AudioCaptureCallback callback) = 0;
    virtual void stopCapture() = 0;
    virtual bool isCaptureActive() const = 0;

    virtual bool startPlayback(std::string_view device_id, int sample_rate) = 0;
    virtual bool writePlayback(const float* pcm, std::size_t sample_count) = 0;
    virtual void finishPlayback() {}
    virtual void stopPlayback() = 0;
    virtual bool isPlaybackActive() const = 0;
    virtual bool isPlaybackDrained() const = 0;

    virtual void setCaptureVolume(float factor) = 0;
    virtual float captureVolume() const = 0;
    virtual float level() const = 0;
};

} // namespace rook::ports
