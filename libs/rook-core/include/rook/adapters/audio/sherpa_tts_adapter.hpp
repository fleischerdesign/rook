#pragma once

#include "rook/ports/text_to_speech_port.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace rook::adapters::audio {

class SherpaTtsAdapter : public ports::TextToSpeechPort {
public:
    explicit SherpaTtsAdapter(std::string model_path);
    ~SherpaTtsAdapter() override;

    std::string id() const override { return "sherpa-tts"; }
    std::string engineName() const override { return "Sherpa-ONNX"; }

    bool isReady() const override;

    std::vector<ports::VoiceInfo> listVoices() const override;
    void setVoice(std::string_view voice_id) override;

    void speak(std::string_view text,
               std::function<void(const float* pcm, std::size_t sample_count,
                                  int sample_rate, bool is_last)> on_chunk) override;

    void stop() override;

    static std::string defaultModelPath();
    static std::string defaultModelUrl();
    using ProgressFn = std::function<void(float progress)>;
    using DoneFn = std::function<void(bool success)>;
    void downloadModel(ProgressFn on_progress, DoneFn on_done);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::adapters::audio
