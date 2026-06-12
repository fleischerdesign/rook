#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rook::ports {

struct SttResult {
    std::string transcript;
    bool is_final = false;
};

class SpeechToTextPort {
public:
    virtual ~SpeechToTextPort() = default;

    virtual std::string id() const = 0;
    virtual std::string engineName() const = 0;

    virtual bool isReady() const = 0;

    virtual void transcribe(
        const int16_t* audio,
        std::size_t sample_count,
        int sample_rate,
        std::function<void(SttResult)> on_result
    ) = 0;

    virtual void cancel() = 0;

    virtual void setModel(std::string_view path) = 0;
    virtual std::vector<std::string> availableModels() const = 0;

    virtual bool supportsStreaming() const { return false; }
    virtual void beginStream() {}
    virtual void acceptWaveform(const int16_t*, int, int) {}
    virtual bool isStreamReady() { return false; }
    virtual void decodeStream() {}
    virtual std::string getPartialResult() { return ""; }
    virtual bool isStreamEndpoint() { return false; }
    virtual void endStream() {}
};

} // namespace rook::ports
