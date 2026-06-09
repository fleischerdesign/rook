#include <glib/gi18n.h>
#include "voice_settings_page.hpp"
#include <peel/Adw/Adw.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string>
#include <future>

#include "rook/adapters/audio/whisper_adapter.hpp"
#include "rook/adapters/audio/piper_adapter.hpp"
#include "rook/adapters/audio/openwakeword_adapter.hpp"

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

void VoiceSettingsPage::addEngineRow(Adw::PreferencesGroup &group,
                                      std::string_view name,
                                      bool ready,
                                      std::string_view status_msg,
                                      std::function<void(VoiceProgressFn, VoiceDoneFn)> on_download)
{
    auto row = Adw::ActionRow::create();
    row->set_title(std::string(name).c_str());
    row->set_subtitle(std::string(status_msg).c_str());
    row->set_activatable(false);

    if (ready) {
        auto icon = Gtk::Image::create_from_icon_name("emblem-ok-symbolic");
        row->add_suffix(std::move(icon).release_floating_ptr());
    } else if (on_download) {
        auto suffix_stack = Gtk::Stack::create();

        auto download_btn = Gtk::Button::create_with_label(_("Download Model"));
        download_btn->add_css_class("pill");
        suffix_stack->add_named(std::move(download_btn).release_floating_ptr(), "button");

        auto prog_box = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
        auto prog_bar = Gtk::ProgressBar::create();
        prog_bar->set_hexpand(true);
        prog_bar->set_valign(Gtk::Align::CENTER);
        auto prog_label = Gtk::Label::create("0%");
        prog_label->add_css_class("dim-label");
        prog_box->append(std::move(prog_bar).release_floating_ptr());
        prog_box->append(std::move(prog_label).release_floating_ptr());
        suffix_stack->add_named(std::move(prog_box).release_floating_ptr(), "progress");

        auto done_icon = Gtk::Image::create_from_icon_name("emblem-ok-symbolic");
        suffix_stack->add_named(std::move(done_icon).release_floating_ptr(), "done");

        suffix_stack->set_visible_child_name("button");

        auto* raw_stack = static_cast<Gtk::Stack*>(suffix_stack);
        row->add_suffix(std::move(suffix_stack).release_floating_ptr());

        auto* btn_page = gtk_stack_get_child_by_name(
            reinterpret_cast<::GtkStack*>(raw_stack), "button");
        auto* raw_btn = GTK_BUTTON(gtk_widget_get_first_child(
            GTK_WIDGET(btn_page)));

        auto* pb_page = gtk_stack_get_child_by_name(
            reinterpret_cast<::GtkStack*>(raw_stack), "progress");
        auto* pb_child = gtk_widget_get_first_child(GTK_WIDGET(pb_page));
        auto* raw_prog_bar = GTK_PROGRESS_BAR(pb_child);
        auto* raw_prog_label = GTK_LABEL(gtk_widget_get_next_sibling(pb_child));

        g_signal_connect_data(raw_btn, "clicked",
            G_CALLBACK(+[](GtkButton*, gpointer data) {
                auto* d = static_cast<std::function<void()>*>(data);
                (*d)();
            }),
            new std::function<void()>([on_download = std::move(on_download),
                                        raw_stack, raw_prog_bar, raw_prog_label]() mutable {
                gtk_stack_set_visible_child_name(reinterpret_cast<::GtkStack*>(raw_stack),
                                                  "progress");
                on_download(
                    [raw_prog_bar, raw_prog_label](float p) {
                        GLib::idle_add_once([raw_prog_bar, raw_prog_label, p]() {
                            gtk_progress_bar_set_fraction(raw_prog_bar, p);
                            auto s = std::to_string(static_cast<int>(p * 100)) + "%";
                            gtk_label_set_text(raw_prog_label, s.c_str());
                        });
                    },
                    [raw_stack](bool ok) {
                        if (ok) {
                            GLib::idle_add_once([raw_stack]() {
                                gtk_stack_set_visible_child_name(
                                    reinterpret_cast<::GtkStack*>(raw_stack), "done");
                            });
                        } else {
                            GLib::idle_add_once([raw_stack]() {
                                gtk_stack_set_visible_child_name(
                                    reinterpret_cast<::GtkStack*>(raw_stack), "button");
                            });
                        }
                    });
            }),
            +[](void* p, GClosure*) { delete static_cast<std::function<void()>*>(p); },
            GConnectFlags(0));
    } else {
        auto icon = Gtk::Image::create_from_icon_name("dialog-warning-symbolic");
        row->add_suffix(std::move(icon).release_floating_ptr());
    }

    group.add(std::move(row).release_floating_ptr());
}

void VoiceSettingsPage::populate(Adw::PreferencesGroup &group)
{
    m_group = &group;

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
        auto* ww = m_wakeword;
        addEngineRow(group, m_wakeword->engineName(), ready,
            ready ? _("Ready — wake word detection active")
                  : (m_wakeword->needsKey()
                        ? _("Access key required")
                        : _("Engine not available — check installation")),
            ready ? std::function<void(VoiceProgressFn, VoiceDoneFn)>{} 
                  : std::function<void(VoiceProgressFn, VoiceDoneFn)>{
                      [ww](VoiceProgressFn p, VoiceDoneFn d) {
                          auto* oww = dynamic_cast<rook::adapters::audio::OpenWakeWordAdapter*>(ww);
                          if (oww) oww->downloadModel(p, d);
                      }});
    } else {
        addEngineRow(group, _("Wake Word"), false, _("No engine loaded"), {});
    }

    if (m_stt) {
        auto ready = m_stt->isReady();
        auto* stt = m_stt;
        addEngineRow(group, m_stt->engineName(), ready,
            ready ? _("Ready — speech recognition active")
                  : _("Engine not available — check installation"),
            ready ? std::function<void(VoiceProgressFn, VoiceDoneFn)>{}
                  : std::function<void(VoiceProgressFn, VoiceDoneFn)>{
                      [stt](VoiceProgressFn p, VoiceDoneFn d) {
                          auto* wa = dynamic_cast<rook::adapters::audio::WhisperAdapter*>(stt);
                          if (wa) wa->downloadModel(p, d);
                      }});
    } else {
        addEngineRow(group, _("Speech Recognition"), false, _("No engine loaded"), {});
    }

    if (m_tts) {
        auto ready = m_tts->isReady();
        auto* tts = m_tts;
        addEngineRow(group, m_tts->engineName(), ready,
            ready ? _("Ready — text-to-speech active")
                  : _("Engine not available — check installation"),
            ready ? std::function<void(VoiceProgressFn, VoiceDoneFn)>{}
                  : std::function<void(VoiceProgressFn, VoiceDoneFn)>{
                      [tts](VoiceProgressFn p, VoiceDoneFn d) {
                          auto* pa = dynamic_cast<rook::adapters::audio::PiperAdapter*>(tts);
                          if (pa) pa->downloadModel(p, d);
                      }});
    } else {
        addEngineRow(group, _("Text-to-Speech"), false, _("No engine loaded"), {});
    }

    if (m_audio_device) {
        auto inputs = m_audio_device->enumerateInputs();
        auto outputs = m_audio_device->enumerateOutputs();
        auto ok = !inputs.empty() && !outputs.empty();
        std::string text = ok
            ? std::string(_("Inputs: ")) + std::to_string(inputs.size()) +
              std::string(_(", Outputs: ")) + std::to_string(outputs.size())
            : std::string(_("No audio devices found"));
        addEngineRow(group, _("Audio Devices"), ok, text, {});
    }
}

} // namespace rook::gui
