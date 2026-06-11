#pragma once

#include "rook/ports/speech_to_text_port.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rook::adapters::audio {

class SherpaAsrAdapter : public ports::SpeechToTextPort {
public:
    struct ModelInfo {
        std::string id;
        std::string name;
        int64_t size_mb;
    };

    explicit SherpaAsrAdapter(std::string model_path = "");
    ~SherpaAsrAdapter() override;

    std::string id() const override { return "sherpa-asr"; }
    std::string engineName() const override { return "Sherpa-ONNX"; }

    bool isReady() const override;

    void transcribe(const int16_t* audio, std::size_t sample_count,
                    int sample_rate,
                    std::function<void(ports::SttResult)> on_result) override;

    void cancel() override;

    void setModel(std::string_view path) override;
    std::vector<std::string> availableModels() const override;

    void setBackend(std::string_view backend);
    void setLanguage(std::string_view language);
    std::string backend() const;
    std::vector<ModelInfo> modelsForBackend(std::string_view backend) const;
    std::vector<std::string> availableBackends() const;

    static std::string defaultModelDir(std::string_view backend,
                                       std::string_view model_id);
    static std::string defaultModelPath();

    using ProgressFn = std::function<void(float progress)>;
    using DoneFn = std::function<void(bool success)>;
    void downloadModel(ProgressFn on_progress, DoneFn on_done);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::adapters::audio
