#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <peel/GLib/functions.h>
#include <memory>
#include <functional>
#include "rook/ports/wakeword_port.hpp"
#include "rook/ports/speech_to_text_port.hpp"
#include "rook/ports/text_to_speech_port.hpp"
#include "rook/ports/audio_device_port.hpp"

namespace rook::adapters::audio {
class WhisperAdapter;
class PiperAdapter;
class OpenWakeWordAdapter;
}

namespace rook::gui {

using VoiceProgressFn = std::function<void(float progress)>;
using VoiceDoneFn = std::function<void(bool success)>;

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
    ~VoiceSettingsPage();

    void rebuildEngineStatus();

private:
    VoiceSettingsPage() = default;

    rook::ports::WakewordPort* m_wakeword = nullptr;
    rook::ports::SpeechToTextPort* m_stt = nullptr;
    rook::ports::TextToSpeechPort* m_tts = nullptr;
    rook::ports::AudioDevicePort* m_audio_device = nullptr;
    peel::Gtk::ListBox* m_engine_list_raw = nullptr;
    ChangeFn m_on_changed;

    void addEngineRow(peel::Gtk::ListBox &list,
                      std::string_view name,
                      bool ready,
                      std::string_view status_msg,
                      std::function<void(VoiceProgressFn, VoiceDoneFn)> on_download);

    void addSectionHeading(peel::Adw::PreferencesGroup &group,
                           std::string_view text,
                           int margin_top);
};

} // namespace rook::gui
