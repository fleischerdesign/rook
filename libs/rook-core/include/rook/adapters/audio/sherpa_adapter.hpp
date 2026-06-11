#pragma once

#include "rook/ports/text_to_speech_port.hpp"

#include <functional>
#include <memory>
#include <string>

namespace rook::adapters::audio {

using ProgressFn = std::function<void(float progress)>;
using DoneFn = std::function<void(bool success)>;

class SherpaAdapter : public ports::TextToSpeechPort {
public:
    explicit SherpaAdapter(std::string model_path = {},
                           std::string voice_id = {});
    ~SherpaAdapter() override;

    std::string id() const override { return "sherpa-onnx"; }
    std::string engineName() const override { return "Sherpa-ONNX"; }
    bool isReady() const override;

    std::vector<ports::VoiceInfo> listVoices() const override;
    void setVoice(std::string_view voice_id) override;

    void speak(std::string_view text,
               std::function<void(const float* pcm, std::size_t sample_count,
                                  int sample_rate, bool is_last)> on_chunk) override;

    void stop() override;

    std::string defaultModelPath() const;
    std::string defaultModelUrl() const;
    void downloadModel(ProgressFn on_progress, DoneFn on_done);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::adapters::audio
