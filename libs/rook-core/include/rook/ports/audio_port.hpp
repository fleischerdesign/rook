#pragma once

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace rook::ports {

enum class AudioState {
    Idle,
    WakeListening,
    Listening,
    Processing,
    Speaking,
};

class AudioPort {
public:
    virtual ~AudioPort() = default;

    virtual void startWakeWordDetection(
        std::function<void()> on_wake
    ) = 0;

    virtual void stopWakeWordDetection() = 0;

    virtual void startSpeechRecognition(
        std::function<void(std::string_view transcript, bool is_final)> on_result
    ) = 0;

    virtual void stopSpeechRecognition() = 0;

    virtual void speak(
        std::string_view text,
        std::function<void()> on_done
    ) = 0;

    virtual void stopSpeaking() = 0;

    virtual void setStateCallback(
        std::function<void(AudioState)> callback
    ) = 0;
};

} // namespace rook::ports
