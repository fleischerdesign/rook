#pragma once

#include "rook/ports/text_to_speech_port.hpp"

#include <memory>
#include <string>

namespace rook::adapters::audio {

class PiperAdapter : public ports::TextToSpeechPort {
public:
    explicit PiperAdapter(std::string model_path = {},
                          std::string voice_id = {});
    ~PiperAdapter() override;

    std::string id() const override { return "piper"; }
    std::string engineName() const override { return "piper"; }
    bool isReady() const override;

    std::vector<ports::VoiceInfo> listVoices() const override;
    void setVoice(std::string_view voice_id) override;

    void speak(std::string_view text,
               std::function<void(const float* pcm, std::size_t sample_count,
                                  int sample_rate, bool is_last)> on_chunk) override;

    void stop() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::adapters::audio
