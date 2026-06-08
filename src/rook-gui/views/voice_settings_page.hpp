#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <memory>
#include <functional>
#include "rook/ports/wakeword_port.hpp"
#include "rook/ports/speech_to_text_port.hpp"
#include "rook/ports/text_to_speech_port.hpp"
#include "rook/ports/audio_device_port.hpp"

namespace rook::gui {

class VoiceSettingsPage {
public:
    using ChangeFn = std::function<void()>;

    static std::unique_ptr<VoiceSettingsPage> create(
        rook::ports::WakewordPort* wakeword = nullptr,
        rook::ports::SpeechToTextPort* stt = nullptr,
        rook::ports::TextToSpeechPort* tts = nullptr,
        rook::ports::AudioDevicePort* audio_device = nullptr,
        ChangeFn on_changed = {});

    void populate(peel::Adw::PreferencesGroup &group);

private:
    VoiceSettingsPage() = default;

    rook::ports::WakewordPort* m_wakeword = nullptr;
    rook::ports::SpeechToTextPort* m_stt = nullptr;
    rook::ports::TextToSpeechPort* m_tts = nullptr;
    rook::ports::AudioDevicePort* m_audio_device = nullptr;
    ChangeFn m_on_changed;

    void addStatusRow(peel::Adw::PreferencesGroup &group,
                      std::string_view label,
                      std::string_view status,
                      bool ok);
};

} // namespace rook::gui
