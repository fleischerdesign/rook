#pragma once

#include "rook/ports/speech_to_text_port.hpp"

#include <memory>
#include <string>

namespace rook::adapters::audio {

class WhisperAdapter : public ports::SpeechToTextPort {
public:
    explicit WhisperAdapter(std::string model_path = {});
    ~WhisperAdapter() override;

    std::string id() const override { return "whisper"; }
    std::string engineName() const override { return "whisper.cpp"; }
    bool isReady() const override;

    void transcribe(const int16_t* audio, std::size_t sample_count,
                    int sample_rate,
                    std::function<void(ports::SttResult)> on_result) override;

    void cancel() override;
    void setModel(std::string_view path) override;
    std::vector<std::string> availableModels() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::adapters::audio
