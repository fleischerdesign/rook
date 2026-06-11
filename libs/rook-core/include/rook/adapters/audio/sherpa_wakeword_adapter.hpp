#pragma once

#include "rook/ports/wakeword_port.hpp"

#include <atomic>
#include <functional>
#include <memory>

namespace rook::adapters::audio {

class SherpaWakewordAdapter : public ports::WakewordPort {
public:
    SherpaWakewordAdapter();
    ~SherpaWakewordAdapter() override;

    std::string id() const override { return "sherpa-wakeword"; }
    std::string engineName() const override { return "Sherpa-ONNX VAD"; }

    bool isReady() const override { return true; }
    bool needsKey() const override { return false; }
    std::size_t frameSize() const override { return 512; }

    bool processFrame(const int16_t* pcm) override;
    void reset() override;
    void setSensitivity(float value) override;

    static std::string defaultModelPath();
    static std::string defaultModelUrl();
    using ProgressFn = std::function<void(float progress)>;
    using DoneFn = std::function<void(bool success)>;
    static void downloadModel(ProgressFn on_progress, DoneFn on_done);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::adapters::audio
