#pragma once

#include "rook/ports/wakeword_port.hpp"

#include <memory>
#include <string>

namespace rook::adapters::audio {

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

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::adapters::audio
