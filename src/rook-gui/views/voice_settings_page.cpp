#include <glib/gi18n.h>
#include "voice_settings_page.hpp"
#include <peel/Adw/Adw.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string>

using namespace peel;

namespace rook::gui {

std::unique_ptr<VoiceSettingsPage> VoiceSettingsPage::create(
    rook::ports::WakewordPort* wakeword,
    rook::ports::SpeechToTextPort* stt,
    rook::ports::TextToSpeechPort* tts,
    rook::ports::AudioDevicePort* audio_device,
    ChangeFn on_changed)
{
    auto page = std::unique_ptr<VoiceSettingsPage>(new VoiceSettingsPage());
    page->m_wakeword = wakeword;
    page->m_stt = stt;
    page->m_tts = tts;
    page->m_audio_device = audio_device;
    page->m_on_changed = std::move(on_changed);
    return page;
}

void VoiceSettingsPage::addStatusRow(Adw::PreferencesGroup &group,
                                      std::string_view label,
                                      std::string_view status,
                                      bool ok)
{
    auto row = Adw::ActionRow::create();
    row->set_title(std::string(label).c_str());
    row->set_subtitle(std::string(status).c_str());

    auto icon = Gtk::Image::create_from_icon_name(
        ok ? "emblem-ok-symbolic" : "dialog-warning-symbolic");
    row->add_suffix(std::move(icon).release_floating_ptr());
    row->set_activatable(false);

    group.add(std::move(row).release_floating_ptr());
}

void VoiceSettingsPage::populate(Adw::PreferencesGroup &group)
{
    auto heading = Gtk::Label::create(_("Voice"));
    heading->set_xalign(0.0f);
    heading->add_css_class("title-2");
    group.add(std::move(heading).release_floating_ptr());

    auto desc = Gtk::Label::create(
        _("Configure voice control — wake word detection, speech recognition, and text-to-speech."));
    desc->set_xalign(0.0f);
    desc->add_css_class("dim-label");
    desc->set_wrap(true);
    desc->set_margin_bottom(8);
    group.add(std::move(desc).release_floating_ptr());

    GSettings* settings = g_settings_new("io.github.fleischerdesign.Rook");

    auto voice_switch = Adw::SwitchRow::create();
    voice_switch->set_title(_("Wake Word"));
    voice_switch->set_subtitle(_("Listen for the wake word to activate voice input"));
    auto* raw_switch = reinterpret_cast<::AdwSwitchRow*>(
        static_cast<peel::Adw::SwitchRow*>(voice_switch));
    adw_switch_row_set_active(raw_switch,
        g_settings_get_boolean(settings, "wake-word-enabled"));

    g_signal_connect(raw_switch, "notify::active",
        G_CALLBACK(+[](::AdwSwitchRow* sw, GParamSpec*, gpointer data) {
            auto* s = static_cast<GSettings*>(data);
            g_settings_set_boolean(s, "wake-word-enabled",
                adw_switch_row_get_active(sw));
            g_settings_sync();
        }), settings);

    group.add(std::move(voice_switch).release_floating_ptr());

    auto sechead = Gtk::Label::create(_("Engine Status"));
    sechead->set_xalign(0.0f);
    sechead->add_css_class("heading");
    sechead->set_margin_top(12);
    group.add(std::move(sechead).release_floating_ptr());

    if (m_wakeword) {
        auto ready = m_wakeword->isReady();
        addStatusRow(group,
            m_wakeword->engineName(),
            ready ? _("Ready — wake word detection active")
                  : (m_wakeword->needsKey()
                        ? _("Access key required")
                        : _("Engine not available — check installation")),
            ready);
    } else {
        addStatusRow(group, _("Wake Word"), _("No engine loaded"), false);
    }

    if (m_stt) {
        auto ready = m_stt->isReady();
        addStatusRow(group,
            m_stt->engineName(),
            ready ? _("Ready — speech recognition active")
                  : _("Engine not available — check installation"),
            ready);
    } else {
        addStatusRow(group, _("Speech Recognition"), _("No engine loaded"), false);
    }

    if (m_tts) {
        auto ready = m_tts->isReady();
        addStatusRow(group,
            m_tts->engineName(),
            ready ? _("Ready — text-to-speech active")
                  : _("Engine not available — check installation"),
            ready);
    } else {
        addStatusRow(group, _("Text-to-Speech"), _("No engine loaded"), false);
    }

    if (m_audio_device) {
        auto inputs = m_audio_device->enumerateInputs();
        auto outputs = m_audio_device->enumerateOutputs();
        auto ok = !inputs.empty() && !outputs.empty();
        std::string text = ok
            ? std::string(_("Inputs: ")) + std::to_string(inputs.size()) +
              std::string(_(", Outputs: ")) + std::to_string(outputs.size())
            : std::string(_("No audio devices found"));
        addStatusRow(group, _("Audio Devices"), text, ok);
    }

}

} // namespace rook::gui
