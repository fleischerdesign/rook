#include <glib/gi18n.h>
#include "voice_settings_page.hpp"
#include <peel/Adw/Adw.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string>

#include "rook/adapters/audio/whisper_adapter.hpp"
#include "rook/adapters/audio/piper_adapter.hpp"
#include "rook/adapters/audio/openwakeword_adapter.hpp"

using namespace peel;

namespace rook::gui {

struct DeviceData {
    GSettings* settings;
    std::vector<rook::ports::DeviceInfo> devices;
};

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

void VoiceSettingsPage::addSectionHeading(Adw::PreferencesGroup &group,
                                           std::string_view text,
                                           int margin_top)
{
    auto label = Gtk::Label::create("");
    label->set_xalign(0.0f);
    label->set_use_markup(true);
    auto markup = "<span weight=\"bold\" size=\"small\">" +
                  std::string(text) + "</span>";
    label->set_markup(markup.c_str());
    label->set_margin_top(margin_top);
    group.add(std::move(label).release_floating_ptr());
}

void VoiceSettingsPage::addEngineRow(Gtk::ListBox &list,
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

    list.append(std::move(row).release_floating_ptr());
}

void VoiceSettingsPage::rebuildEngineStatus()
{
    if (!m_engine_list_raw) return;

    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(m_engine_list_raw))))
        gtk_list_box_remove(m_engine_list_raw, child);

    auto& list = *reinterpret_cast<peel::Gtk::ListBox*>(m_engine_list_raw);

    if (m_wakeword) {
        auto ready = m_wakeword->isReady();
        auto* ww = m_wakeword;
        addEngineRow(list, m_wakeword->engineName(), ready,
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
        addEngineRow(list, _("Wake Word"), false, _("No engine loaded"), {});
    }

    if (m_stt) {
        auto ready = m_stt->isReady();
        auto* stt = m_stt;
        addEngineRow(list, m_stt->engineName(), ready,
            ready ? _("Ready — speech recognition active")
                  : _("Engine not available — check installation"),
            ready ? std::function<void(VoiceProgressFn, VoiceDoneFn)>{}
                  : std::function<void(VoiceProgressFn, VoiceDoneFn)>{
                      [stt](VoiceProgressFn p, VoiceDoneFn d) {
                          auto* wa = dynamic_cast<rook::adapters::audio::WhisperAdapter*>(stt);
                          if (wa) wa->downloadModel(p, d);
                      }});
    } else {
        addEngineRow(list, _("Speech Recognition"), false, _("No engine loaded"), {});
    }

    if (m_tts) {
        auto ready = m_tts->isReady();
        auto* tts = m_tts;
        addEngineRow(list, m_tts->engineName(), ready,
            ready ? _("Ready — text-to-speech active")
                  : _("Engine not available — check installation"),
            ready ? std::function<void(VoiceProgressFn, VoiceDoneFn)>{}
                  : std::function<void(VoiceProgressFn, VoiceDoneFn)>{
                      [tts](VoiceProgressFn p, VoiceDoneFn d) {
                          auto* pa = dynamic_cast<rook::adapters::audio::PiperAdapter*>(tts);
                          if (pa) pa->downloadModel(p, d);
                      }});
    } else {
        addEngineRow(list, _("Text-to-Speech"), false, _("No engine loaded"), {});
    }
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
    desc->set_margin_bottom(4);
    group.add(std::move(desc).release_floating_ptr());

    GSettings* settings = g_settings_new("io.github.fleischerdesign.Rook");

    addSectionHeading(group, _("Wake Word"), 8);

    auto wake_list = Gtk::ListBox::create();
    wake_list->add_css_class("boxed-list");
    wake_list->set_selection_mode(Gtk::SelectionMode::NONE);

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
    wake_list->append(std::move(voice_switch).release_floating_ptr());
    group.add(std::move(wake_list).release_floating_ptr());

    addSectionHeading(group, _("Engine Status"), 16);

    auto engine_list = Gtk::ListBox::create();
    engine_list->add_css_class("boxed-list");
    engine_list->set_selection_mode(Gtk::SelectionMode::NONE);

    m_engine_list_raw = GTK_LIST_BOX(reinterpret_cast<::GObject*>(static_cast<Gtk::ListBox*>(engine_list)));

    if (m_wakeword) {
        auto ready = m_wakeword->isReady();
        auto* ww = m_wakeword;
        addEngineRow(*engine_list, m_wakeword->engineName(), ready,
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
        addEngineRow(*engine_list, _("Wake Word"), false, _("No engine loaded"), {});
    }

    if (m_stt) {
        auto ready = m_stt->isReady();
        auto* stt = m_stt;
        addEngineRow(*engine_list, m_stt->engineName(), ready,
            ready ? _("Ready — speech recognition active")
                  : _("Engine not available — check installation"),
            ready ? std::function<void(VoiceProgressFn, VoiceDoneFn)>{}
                  : std::function<void(VoiceProgressFn, VoiceDoneFn)>{
                      [stt](VoiceProgressFn p, VoiceDoneFn d) {
                          auto* wa = dynamic_cast<rook::adapters::audio::WhisperAdapter*>(stt);
                          if (wa) wa->downloadModel(p, d);
                      }});
    } else {
        addEngineRow(*engine_list, _("Speech Recognition"), false, _("No engine loaded"), {});
    }

    if (m_tts) {
        auto ready = m_tts->isReady();
        auto* tts = m_tts;
        addEngineRow(*engine_list, m_tts->engineName(), ready,
            ready ? _("Ready — text-to-speech active")
                  : _("Engine not available — check installation"),
            ready ? std::function<void(VoiceProgressFn, VoiceDoneFn)>{}
                  : std::function<void(VoiceProgressFn, VoiceDoneFn)>{
                      [tts](VoiceProgressFn p, VoiceDoneFn d) {
                          auto* pa = dynamic_cast<rook::adapters::audio::PiperAdapter*>(tts);
                          if (pa) pa->downloadModel(p, d);
                      }});
    } else {
        addEngineRow(*engine_list, _("Text-to-Speech"), false, _("No engine loaded"), {});
    }

    group.add(std::move(engine_list).release_floating_ptr());

    addSectionHeading(group, _("Speech Recognition"), 16);

    auto whisper_list = Gtk::ListBox::create();
    whisper_list->add_css_class("boxed-list");
    whisper_list->set_selection_mode(Gtk::SelectionMode::NONE);

    auto model_row = Adw::ComboRow::create();
    model_row->set_title(_("Whisper Model"));
    model_row->set_subtitle(_("Smaller = faster, larger = more accurate"));

    const char* models_raw[] = {"tiny", "base", "small", "medium", "large-v3", nullptr};
    auto model_model = Gtk::StringList::create(models_raw);
    adw_combo_row_set_model(
        reinterpret_cast<::AdwComboRow*>(static_cast<peel::Adw::ComboRow*>(model_row)),
        G_LIST_MODEL(reinterpret_cast<::GtkStringList*>(
            static_cast<Gtk::StringList*>(model_model))));

    char* saved_model = g_settings_get_string(settings, "whisper-model");
    int model_idx = 2;
    if (saved_model && saved_model[0]) {
        for (int i = 0; i < 5; ++i) {
            if (std::string(models_raw[i]) == saved_model) { model_idx = i; break; }
        }
    }
    g_free(saved_model);
    model_row->set_selected(static_cast<unsigned>(model_idx));

    auto* raw_model = reinterpret_cast<::AdwComboRow*>(
        static_cast<peel::Adw::ComboRow*>(model_row));
    g_signal_connect(raw_model, "notify::selected",
        G_CALLBACK(+[](::AdwComboRow* row, GParamSpec*, gpointer data) {
            auto* self = static_cast<VoiceSettingsPage*>(data);
            guint sel = adw_combo_row_get_selected(row);
            if (sel < 5) {
                const char* nm = "small";
                switch (sel) {
                    case 0: nm = "tiny"; break;
                    case 1: nm = "base"; break;
                    case 2: nm = "small"; break;
                    case 3: nm = "medium"; break;
                    case 4: nm = "large-v3"; break;
                }
                auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
                g_settings_set_string(gs, "whisper-model", nm);
                g_object_unref(gs);
            }
            g_settings_sync();
            self->rebuildEngineStatus();
            if (self->m_on_changed) self->m_on_changed();
        }), this);
    whisper_list->append(std::move(model_row).release_floating_ptr());

    auto thread_row = Adw::SpinRow::create_with_range(1.0, 16.0, 1.0);
    thread_row->set_title(_("CPU Threads"));
    thread_row->set_subtitle(_("Threads for whisper inference (1–16)"));
    thread_row->set_digits(0);
    auto* raw_th = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(thread_row));
    adw_spin_row_set_value(raw_th,
        static_cast<double>(g_settings_get_int(settings, "whisper-threads")));
    g_signal_connect(raw_th, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = static_cast<GSettings*>(data);
            g_settings_set_int(s, "whisper-threads",
                static_cast<int>(adw_spin_row_get_value(row)));
            g_settings_sync();
        }), settings);
    whisper_list->append(std::move(thread_row).release_floating_ptr());

    auto beam_row = Adw::SpinRow::create_with_range(1.0, 5.0, 1.0);
    beam_row->set_title(_("Beam Size"));
    beam_row->set_subtitle(_("Beam search width. 1 = fastest, 5 = best accuracy"));
    beam_row->set_digits(0);
    auto* raw_beam = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(beam_row));
    adw_spin_row_set_value(raw_beam,
        static_cast<double>(g_settings_get_int(settings, "whisper-beam-size")));
    g_signal_connect(raw_beam, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = static_cast<GSettings*>(data);
            g_settings_set_int(s, "whisper-beam-size",
                static_cast<int>(adw_spin_row_get_value(row)));
            g_settings_sync();
        }), settings);
    whisper_list->append(std::move(beam_row).release_floating_ptr());

    auto bestof_row = Adw::SpinRow::create_with_range(1.0, 5.0, 1.0);
    bestof_row->set_title(_("Best-Of Candidates"));
    bestof_row->set_subtitle(_("Number of best candidates. 1 = fastest"));
    bestof_row->set_digits(0);
    auto* raw_bof = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(bestof_row));
    adw_spin_row_set_value(raw_bof,
        static_cast<double>(g_settings_get_int(settings, "whisper-best-of")));
    g_signal_connect(raw_bof, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = static_cast<GSettings*>(data);
            g_settings_set_int(s, "whisper-best-of",
                static_cast<int>(adw_spin_row_get_value(row)));
            g_settings_sync();
        }), settings);
    whisper_list->append(std::move(bestof_row).release_floating_ptr());

    auto ctx_row = Adw::SpinRow::create_with_range(0.0, 2048.0, 128.0);
    ctx_row->set_title(_("Audio Context"));
    ctx_row->set_subtitle(_("Encoding window size. 0 = auto, 512 = fast"));
    ctx_row->set_digits(0);
    auto* raw_ctx = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(ctx_row));
    adw_spin_row_set_value(raw_ctx,
        static_cast<double>(g_settings_get_int(settings, "whisper-audio-ctx")));
    g_signal_connect(raw_ctx, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = static_cast<GSettings*>(data);
            g_settings_set_int(s, "whisper-audio-ctx",
                static_cast<int>(adw_spin_row_get_value(row)));
            g_settings_sync();
        }), settings);
    whisper_list->append(std::move(ctx_row).release_floating_ptr());

    auto nst_switch = Adw::SwitchRow::create();
    nst_switch->set_title(_("Suppress Noise Markers"));
    nst_switch->set_subtitle(_("Filter [Music], [Applause], [Laughter] from transcriptions"));
    auto* raw_nst = reinterpret_cast<::AdwSwitchRow*>(
        static_cast<peel::Adw::SwitchRow*>(nst_switch));
    adw_switch_row_set_active(raw_nst,
        g_settings_get_boolean(settings, "whisper-suppress-nst"));
    g_signal_connect(raw_nst, "notify::active",
        G_CALLBACK(+[](::AdwSwitchRow* sw, GParamSpec*, gpointer data) {
            auto* s = static_cast<GSettings*>(data);
            g_settings_set_boolean(s, "whisper-suppress-nst",
                adw_switch_row_get_active(sw));
            g_settings_sync();
        }), settings);
    whisper_list->append(std::move(nst_switch).release_floating_ptr());

    auto thold_row = Adw::SpinRow::create_with_range(0.0, 1.0, 0.05);
    thold_row->set_title(_("No-Speech Threshold"));
    thold_row->set_subtitle(_("Higher = less false positives in silence"));
    thold_row->set_digits(2);
    auto* raw_thold = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(thold_row));
    adw_spin_row_set_value(raw_thold,
        g_settings_get_double(settings, "whisper-no-speech-thold"));
    g_signal_connect(raw_thold, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = static_cast<GSettings*>(data);
            g_settings_set_double(s, "whisper-no-speech-thold",
                adw_spin_row_get_value(row));
            g_settings_sync();
        }), settings);
    whisper_list->append(std::move(thold_row).release_floating_ptr());

    auto ent_row = Adw::SpinRow::create_with_range(0.0, 6.0, 0.1);
    ent_row->set_title(_("Entropy Threshold"));
    ent_row->set_subtitle(_("Higher = less low-confidence hallucinated text"));
    ent_row->set_digits(2);
    auto* raw_ent = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(ent_row));
    adw_spin_row_set_value(raw_ent,
        g_settings_get_double(settings, "whisper-entropy-thold"));
    g_signal_connect(raw_ent, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = static_cast<GSettings*>(data);
            g_settings_set_double(s, "whisper-entropy-thold",
                adw_spin_row_get_value(row));
            g_settings_sync();
        }), settings);
    whisper_list->append(std::move(ent_row).release_floating_ptr());

    auto nf_switch = Adw::SwitchRow::create();
    nf_switch->set_title(_("No Temperature Fallback"));
    nf_switch->set_subtitle(_("Prevents whisper from guessing when uncertain"));
    auto* raw_nf = reinterpret_cast<::AdwSwitchRow*>(
        static_cast<peel::Adw::SwitchRow*>(nf_switch));
    adw_switch_row_set_active(raw_nf,
        g_settings_get_boolean(settings, "whisper-no-fallback"));
    g_signal_connect(raw_nf, "notify::active",
        G_CALLBACK(+[](::AdwSwitchRow* sw, GParamSpec*, gpointer data) {
            auto* s = static_cast<GSettings*>(data);
            g_settings_set_boolean(s, "whisper-no-fallback",
                adw_switch_row_get_active(sw));
            g_settings_sync();
        }), settings);
    whisper_list->append(std::move(nf_switch).release_floating_ptr());

    group.add(std::move(whisper_list).release_floating_ptr());

    addSectionHeading(group, _("Audio Capture"), 16);

    auto capture_list = Gtk::ListBox::create();
    capture_list->add_css_class("boxed-list");
    capture_list->set_selection_mode(Gtk::SelectionMode::NONE);

    auto sil_row = Adw::SpinRow::create_with_range(0.0, 500.0, 10.0);
    sil_row->set_title(_("Silence Threshold"));
    sil_row->set_subtitle(_("Energy below this = silence. 0 = never skip, 80 = quiet room"));
    sil_row->set_digits(0);
    auto* raw_sil = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(sil_row));
    adw_spin_row_set_value(raw_sil,
        g_settings_get_double(settings, "voice-silence-threshold"));
    g_signal_connect(raw_sil, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = static_cast<GSettings*>(data);
            g_settings_set_double(s, "voice-silence-threshold",
                adw_spin_row_get_value(row));
            g_settings_sync();
        }), settings);
    capture_list->append(std::move(sil_row).release_floating_ptr());

    group.add(std::move(capture_list).release_floating_ptr());

    if (m_audio_device) {
        auto inputs = m_audio_device->enumerateInputs();
        auto outputs = m_audio_device->enumerateOutputs();

        addSectionHeading(group, _("Audio Devices"), 16);

        auto dev_list = Gtk::ListBox::create();
        dev_list->add_css_class("boxed-list");
        dev_list->set_selection_mode(Gtk::SelectionMode::NONE);

        auto mic_row = Adw::ComboRow::create();
        mic_row->set_title(_("Input Device"));

        const char* no_items[] = {nullptr};
        auto mic_model = Gtk::StringList::create(no_items);
        int mic_default_idx = 0;
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            auto label = inputs[i].name;
            if (label.empty()) label = _("Default");
            mic_model->append(label.c_str());
            if (inputs[i].is_default) mic_default_idx = static_cast<int>(i);
        }
        if (inputs.empty())
            mic_model->append(_("Default"));
        adw_combo_row_set_model(
            reinterpret_cast<::AdwComboRow*>(static_cast<peel::Adw::ComboRow*>(mic_row)),
            G_LIST_MODEL(reinterpret_cast<::GtkStringList*>(
                static_cast<Gtk::StringList*>(mic_model))));

        char* saved_mic = g_settings_get_string(settings, "microphone-device");
        if (saved_mic && saved_mic[0]) {
            for (std::size_t i = 0; i < inputs.size(); ++i) {
                if (inputs[i].id == saved_mic) {
                    mic_default_idx = static_cast<int>(i);
                    break;
                }
            }
        }
        g_free(saved_mic);
        mic_row->set_selected(static_cast<unsigned>(mic_default_idx));

        auto* raw_mic = reinterpret_cast<::AdwComboRow*>(
            static_cast<peel::Adw::ComboRow*>(mic_row));
        auto* dd_mic = new DeviceData{settings, inputs};
        g_signal_connect(raw_mic, "notify::selected",
            G_CALLBACK(+[](::AdwComboRow* row, GParamSpec*, gpointer data) {
                auto* d = static_cast<DeviceData*>(data);
                guint sel = adw_combo_row_get_selected(row);
                if (sel < d->devices.size()) {
                    g_settings_set_string(d->settings, "microphone-device",
                                          d->devices[sel].id.c_str());
                } else {
                    g_settings_set_string(d->settings, "microphone-device", "");
                }
                g_settings_sync();
            }),
            dd_mic);
        (void)raw_mic;

        dev_list->append(std::move(mic_row).release_floating_ptr());

        auto spk_row = Adw::ComboRow::create();
        spk_row->set_title(_("Output Device"));

        auto spk_model = Gtk::StringList::create(no_items);
        int spk_default_idx = 0;
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            auto label = outputs[i].name;
            if (label.empty()) label = _("Default");
            spk_model->append(label.c_str());
            if (outputs[i].is_default) spk_default_idx = static_cast<int>(i);
        }
        if (outputs.empty())
            spk_model->append(_("Default"));
        adw_combo_row_set_model(
            reinterpret_cast<::AdwComboRow*>(static_cast<peel::Adw::ComboRow*>(spk_row)),
            G_LIST_MODEL(reinterpret_cast<::GtkStringList*>(
                static_cast<Gtk::StringList*>(spk_model))));

        char* saved_spk = g_settings_get_string(settings, "speaker-device");
        if (saved_spk && saved_spk[0]) {
            for (std::size_t i = 0; i < outputs.size(); ++i) {
                if (outputs[i].id == saved_spk) {
                    spk_default_idx = static_cast<int>(i);
                    break;
                }
            }
        }
        g_free(saved_spk);
        spk_row->set_selected(static_cast<unsigned>(spk_default_idx));

        auto* raw_spk = reinterpret_cast<::AdwComboRow*>(
            static_cast<peel::Adw::ComboRow*>(spk_row));
        auto* dd_spk = new DeviceData{settings, outputs};
        g_signal_connect(raw_spk, "notify::selected",
            G_CALLBACK(+[](::AdwComboRow* row, GParamSpec*, gpointer data) {
                auto* d = static_cast<DeviceData*>(data);
                guint sel = adw_combo_row_get_selected(row);
                if (sel < d->devices.size()) {
                    g_settings_set_string(d->settings, "speaker-device",
                                          d->devices[sel].id.c_str());
                } else {
                    g_settings_set_string(d->settings, "speaker-device", "");
                }
                g_settings_sync();
            }),
            dd_spk);
        (void)raw_spk;

        dev_list->append(std::move(spk_row).release_floating_ptr());
        group.add(std::move(dev_list).release_floating_ptr());
    }
}

} // namespace rook::gui
