#pragma once

#include "rook/ports/audio_device_port.hpp"
#include "rook/core/lockfree_ring_buffer.hpp"

struct ma_device;
struct ma_context;

namespace rook::adapters::audio {

class MiniaudioAdapter : public rook::ports::AudioDevicePort {
public:
    MiniaudioAdapter();
    ~MiniaudioAdapter() override;

    MiniaudioAdapter(const MiniaudioAdapter&) = delete;
    MiniaudioAdapter& operator=(const MiniaudioAdapter&) = delete;

    std::vector<ports::DeviceInfo> enumerateInputs() const override;
    std::vector<ports::DeviceInfo> enumerateOutputs() const override;

    bool startCapture(std::string_view device_id,
                      ports::AudioCaptureCallback callback) override;
    void stopCapture() override;
    bool isCaptureActive() const override;

    bool startPlayback(std::string_view device_id, int sample_rate) override;
    bool writePlayback(const float* pcm, std::size_t sample_count) override;
    void finishPlayback();
    void stopPlayback() override;
    bool isPlaybackActive() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::adapters::audio
