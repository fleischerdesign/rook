#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rook::ports {

struct VoiceInfo {
    std::string id;
    std::string name;
    std::string language;
};

class TextToSpeechPort {
public:
    virtual ~TextToSpeechPort() = default;

    virtual std::string id() const = 0;
    virtual std::string engineName() const = 0;

    virtual bool isReady() const = 0;

    virtual std::vector<VoiceInfo> listVoices() const = 0;
    virtual void setVoice(std::string_view voice_id) = 0;

    virtual void speak(
        std::string_view text,
        std::function<void(const float* pcm, std::size_t sample_count,
                           int sample_rate, bool is_last)> on_chunk
    ) = 0;

    virtual void stop() = 0;
};

} // namespace rook::ports
