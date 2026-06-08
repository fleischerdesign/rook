#pragma once

#include "rook/ports/wakeword_port.hpp"
#include "rook/ports/speech_to_text_port.hpp"
#include "rook/ports/text_to_speech_port.hpp"
#include "rook/ports/audio_device_port.hpp"

namespace rook::ports {

enum class AudioState {
    Inactive,
    WaitingForWake,
    Recording,
    Processing,
    Speaking,
};

} // namespace rook::ports
