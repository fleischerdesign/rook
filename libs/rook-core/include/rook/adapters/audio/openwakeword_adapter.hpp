#pragma once

#include "rook/ports/wakeword_port.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rook::adapters::audio {

using ProgressFn = std::function<void(float progress)>;
using DoneFn = std::function<void(bool success)>;

class OpenWakeWordAdapter : public ports::WakewordPort {
public:
    OpenWakeWordAdapter();
    ~OpenWakeWordAdapter() override;

    std::string id() const override { return "openwakeword"; }
    std::string engineName() const override { return "openWakeWord"; }
    bool isReady() const override;
    bool needsKey() const override { return false; }
    std::size_t frameSize() const override;

    bool processFrame(const int16_t* pcm) override;
    void reset() override;
    void setSensitivity(float value) override;

    std::string modelPath() const;
    std::string defaultModelPath() const;
    std::string defaultModelUrl() const;
    void downloadModel(ProgressFn on_progress, DoneFn on_done);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::adapters::audio
