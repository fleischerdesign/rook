#include <glib/gi18n.h>
#include "voice_settings_page.hpp"
#include <peel/Adw/Adw.h>
#include <peel/Gio/Gio.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <string>

#include "rook/adapters/audio/sherpa_asr_adapter.hpp"
#include "rook/adapters/audio/sherpa_tts_adapter.hpp"
#include "rook/adapters/audio/sherpa_wakeword_adapter.hpp"
#include "rook/adapters/model/model_cache.hpp"

using namespace peel;

namespace rook::gui {

using rook::adapters::audio::SherpaAsrAdapter;
using rook::adapters::audio::SherpaTtsAdapter;
using rook::adapters::audio::SherpaWakewordAdapter;

struct DeviceData {
    peel::Gio::Settings* settings;
    std::vector<rook::ports::DeviceInfo> devices;
};

std::unique_ptr<VoiceSettingsPage> VoiceSettingsPage::create(
    rook::ports::WakewordPort* wakeword,
    rook::ports::SpeechToTextPort* stt,
    rook::ports::TextToSpeechPort* tts,
    rook::ports::AudioDevicePort* audio_device,
    rook::ports::LlmPort* llm,
    ChangeFn on_changed)
{
    auto page = std::unique_ptr<VoiceSettingsPage>(new VoiceSettingsPage());
    page->m_wakeword = wakeword;
    page->m_stt = stt;
    page->m_tts = tts;
    page->m_audio_device = audio_device;
    page->m_llm = llm;
    page->m_on_changed = std::move(on_changed);
    return page;
}

VoiceSettingsPage::~VoiceSettingsPage()
{
    if (m_level_timer_id)
        g_source_remove(m_level_timer_id);
    if (m_engine_list_raw)
        g_object_unref(reinterpret_cast<::GObject*>(m_engine_list_raw));
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

    if (on_download) {
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
        auto* raw_btn = GTK_BUTTON(btn_page);

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
                                        raw_stack, raw_prog_bar, raw_prog_label,
                                        name = std::string(name)]() mutable {
                SPDLOG_INFO("VoiceSettings: download button clicked for '{}'", name);
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
    } else if (ready) {
        auto icon = Gtk::Image::create_from_icon_name("emblem-ok-symbolic");
        row->add_suffix(std::move(icon).release_floating_ptr());
    } else {
        auto icon = Gtk::Image::create_from_icon_name("dialog-warning-symbolic");
        row->add_suffix(std::move(icon).release_floating_ptr());
    }

    list.append(std::move(row).release_floating_ptr());
}

void VoiceSettingsPage::rebuildEngineStatus()
{
    if (!m_engine_list_raw) return;

    auto* gobj = reinterpret_cast<::GObject*>(m_engine_list_raw);
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(gobj))))
        gtk_list_box_remove(GTK_LIST_BOX(gobj), child);

    auto& list = *m_engine_list_raw;

    if (m_wakeword) {
        bool has_vad = std::filesystem::exists(
            SherpaWakewordAdapter::defaultModelPath());
        auto* ww = m_wakeword;
        addEngineRow(list, m_wakeword->engineName(), true,
            has_vad ? _("Ready — neural VAD active")
                    : _("Ready — energy-based fallback"),
            has_vad ? std::function<void(VoiceProgressFn, VoiceDoneFn)>{}
                    : std::function<void(VoiceProgressFn, VoiceDoneFn)>{
                          [ww](VoiceProgressFn p, VoiceDoneFn d) {
                              auto* oww = dynamic_cast<SherpaWakewordAdapter*>(ww);
                              if (oww) oww->downloadModel(p, d);
                          }});
    } else {
        addEngineRow(list, _("Wake Word"), false, _("No engine loaded"), {});
    }

    if (m_wakeword) {
        auto* ww = m_wakeword;
        auto* oww = dynamic_cast<SherpaWakewordAdapter*>(ww);
        bool has_kws = oww && oww->hasKws();
        addEngineRow(list, "Keyword Spotter (KWS)", has_kws,
            has_kws ? _("Ready — 'HEY ROOK' keyword active")
                    : _("Not installed"),
            has_kws ? std::function<void(VoiceProgressFn, VoiceDoneFn)>{}
                    : std::function<void(VoiceProgressFn, VoiceDoneFn)>{
                          [ww](VoiceProgressFn p, VoiceDoneFn d) {
                              auto* oww =
                                  dynamic_cast<SherpaWakewordAdapter*>(ww);
                              if (oww) oww->downloadKwsModel(p, d);
                          }});
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
                          auto* wa = dynamic_cast<rook::adapters::audio::SherpaAsrAdapter*>(stt);
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
                          auto* sa = dynamic_cast<rook::adapters::audio::SherpaTtsAdapter*>(tts);
                          if (sa) sa->downloadModel(p, d);
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

    auto settings = Gio::Settings::create("io.github.fleischerdesign.Rook");
    auto* settings_raw = reinterpret_cast<::GSettings*>(
        static_cast<peel::Gio::Settings*>(settings));

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
        settings->get_boolean("wake-word-enabled"));
    g_signal_connect(raw_switch, "notify::active",
        G_CALLBACK(+[](::AdwSwitchRow* sw, GParamSpec*, gpointer data) {
            auto* s = reinterpret_cast<peel::Gio::Settings*>(data);
            s->set_boolean( "wake-word-enabled",
                adw_switch_row_get_active(sw));
            s->sync();
        }), settings_raw);
    wake_list->append(std::move(voice_switch).release_floating_ptr());

    auto sens_row = Adw::SpinRow::create_with_range(0.0, 1.0, 0.05);
    sens_row->set_title(_("Sensitivity"));
    sens_row->set_subtitle(_("Lower = easier to trigger, higher = clearer speech"));
    sens_row->set_digits(2);
    auto* raw_sens = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(sens_row));
    adw_spin_row_set_value(raw_sens,
        settings->get_double("wakeword-sensitivity"));
    g_signal_connect(raw_sens, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = reinterpret_cast<peel::Gio::Settings*>(data);
            s->set_double("wakeword-sensitivity",
                                  adw_spin_row_get_value(row));
            s->sync();
        }), settings_raw);
    wake_list->append(std::move(sens_row).release_floating_ptr());

    group.add(std::move(wake_list).release_floating_ptr());

    if (m_llm) {
        addSectionHeading(group, _("Voice LLM Model"), 16);

        auto model_list = Gtk::ListBox::create();
        model_list->add_css_class("boxed-list");
        model_list->set_selection_mode(Gtk::SelectionMode::NONE);

        auto model_row = Adw::ComboRow::create();
        model_row->set_title(_("Voice Model"));
        model_row->set_subtitle(_("LLM model used for voice wake-word queries"));
        model_row->set_activatable(true);

        RefPtr<Gtk::StringList> model_strings = Gtk::StringList::create({});
        auto& model_ids = m_voice_model_ids;
        model_ids.clear();

        auto providers = m_llm->listProviders();
        for (const auto &prov : providers) {
            if (!prov.enabled) continue;

            auto cached_models = rook::adapters::model::ModelCache::instance().getAllEnabled(
                [&](std::string_view pid) { return pid == prov.id; });

            if (!cached_models.empty()) {
                for (const auto &model : cached_models) {
                    auto id = prov.id + ":" + model.id;
                    auto label = prov.display_name + " / " + model.display_name;
                    model_ids.push_back(id);
                    model_strings->append(label.c_str());
                }
            } else {
                auto info = rook::ports::ProviderRegistry::instance().find(prov.type);
                auto default_model = info ? info->default_model : prov.default_model;
                auto id = prov.id + ":" + default_model;
                auto label = prov.display_name + " / " + default_model;
                model_ids.push_back(id);
                model_strings->append(label.c_str());
            }
        }

        model_row->set_model(model_strings);

        std::string saved_model;
        auto gs2 = Gio::Settings::create("io.github.fleischerdesign.Rook");
        auto saved = gs2->get_string("voice-model");
        if (saved) saved_model = saved.c_str();

        unsigned sel = 0;
        for (unsigned i = 0; i < model_ids.size(); ++i) {
            if (model_ids[i] == saved_model) { sel = i; break; }
        }
        model_row->set_selected(sel);

        auto* raw_mr = reinterpret_cast<::AdwComboRow*>(
            static_cast<peel::Adw::ComboRow*>(model_row));
        g_signal_connect(raw_mr, "notify::selected",
            G_CALLBACK(+[](::AdwComboRow* row, GParamSpec*, gpointer data) {
                auto* ids = static_cast<std::vector<std::string>*>(data);
                guint sel2 = adw_combo_row_get_selected(row);
                if (sel2 < ids->size()) {
                    auto gs3 = Gio::Settings::create("io.github.fleischerdesign.Rook");
                    gs3->set_string("voice-model", (*ids)[sel2].c_str());
                    gs3->sync();
                }
            }), &model_ids);

        model_list->append(std::move(model_row).release_floating_ptr());
        group.add(std::move(model_list).release_floating_ptr());
    }

    addSectionHeading(group, _("Engine Status"), 16);

    auto engine_list = Gtk::ListBox::create();
    engine_list->add_css_class("boxed-list");
    engine_list->set_selection_mode(Gtk::SelectionMode::NONE);

    m_engine_list_raw = engine_list.operator->();
    g_object_ref(reinterpret_cast<::GObject*>(m_engine_list_raw));

    if (m_wakeword) {
        bool has_vad = std::filesystem::exists(
            SherpaWakewordAdapter::defaultModelPath());
        auto* ww = m_wakeword;
        addEngineRow(*engine_list, m_wakeword->engineName(), true,
            has_vad ? _("Ready — neural VAD active")
                    : _("Ready — energy-based fallback"),
            has_vad ? std::function<void(VoiceProgressFn, VoiceDoneFn)>{}
                    : std::function<void(VoiceProgressFn, VoiceDoneFn)>{
                          [ww](VoiceProgressFn p, VoiceDoneFn d) {
                              auto* oww = dynamic_cast<SherpaWakewordAdapter*>(ww);
                              if (oww) oww->downloadModel(p, d);
                          }});
    } else {
        addEngineRow(*engine_list, _("Wake Word"), false, _("No engine loaded"), {});
    }

    if (m_wakeword) {
        auto* ww = m_wakeword;
        auto* oww = dynamic_cast<SherpaWakewordAdapter*>(ww);
        bool has_kws = oww && oww->hasKws();
        addEngineRow(*engine_list, "Keyword Spotter (KWS)", has_kws,
            has_kws ? _("Ready — 'HEY ROOK' keyword active")
                    : _("Not installed"),
            has_kws ? std::function<void(VoiceProgressFn, VoiceDoneFn)>{}
                    : std::function<void(VoiceProgressFn, VoiceDoneFn)>{
                          [ww](VoiceProgressFn p, VoiceDoneFn d) {
                              auto* oww =
                                  dynamic_cast<SherpaWakewordAdapter*>(ww);
                              if (oww) oww->downloadKwsModel(p, d);
                          }});
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
                          auto* wa = dynamic_cast<rook::adapters::audio::SherpaAsrAdapter*>(stt);
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
                          auto* sa = dynamic_cast<rook::adapters::audio::SherpaTtsAdapter*>(tts);
                          if (sa) sa->downloadModel(p, d);
                      }});
    } else {
        addEngineRow(*engine_list, _("Text-to-Speech"), false, _("No engine loaded"), {});
    }

    group.add(std::move(engine_list).release_floating_ptr());

    addSectionHeading(group, _("Speech Recognition"), 16);

    auto asr_list = Gtk::ListBox::create();
    asr_list->add_css_class("boxed-list");
    asr_list->set_selection_mode(Gtk::SelectionMode::NONE);

    auto* asr = dynamic_cast<rook::adapters::audio::SherpaAsrAdapter*>(m_stt);

    auto backend_row = Adw::ComboRow::create();
    backend_row->set_title(_("ASR Backend"));
    backend_row->set_subtitle(_("Speech recognition engine"));
    auto saved_backend = settings->get_string("asr-backend");
    int backend_idx = 0;
    {
        auto backends = asr ? asr->availableBackends()
                            : std::vector<std::string>{"whisper"};
        auto model = Gtk::StringList::create({nullptr});
        for (size_t i = 0; i < backends.size(); ++i) {
            if (saved_backend && backends[i] == saved_backend.c_str())
                backend_idx = static_cast<int>(i);
            model->append(backends[i].c_str());
        }
        backend_row->set_model(model);
    }
    backend_row->set_selected(static_cast<unsigned>(backend_idx));
    auto* raw_backend = reinterpret_cast<::AdwComboRow*>(
        static_cast<peel::Adw::ComboRow*>(backend_row));
    g_signal_connect(raw_backend, "notify::selected",
        G_CALLBACK(+[](::AdwComboRow* row, GParamSpec*, gpointer data) {
            auto* self = static_cast<VoiceSettingsPage*>(data);
            auto gs = Gio::Settings::create("io.github.fleischerdesign.Rook");
            guint sel = adw_combo_row_get_selected(row);
            auto* aa = dynamic_cast<SherpaAsrAdapter*>(self->m_stt);
            auto backends = aa ? aa->availableBackends()
                               : std::vector<std::string>{"whisper"};
            if (sel < backends.size()) {
                gs->set_string("asr-backend",
                                      backends[sel].c_str());
            }
            gs->sync();
            self->rebuildEngineStatus();
            if (self->m_on_changed) self->m_on_changed();
        }), this);
    asr_list->append(std::move(backend_row).release_floating_ptr());

    auto model_row = Adw::ComboRow::create();
    model_row->set_title(_("ASR Model"));
    model_row->set_subtitle(_("Smaller = faster, larger = more accurate"));
    auto saved_model = settings->get_string("asr-model");
    auto current_backend = settings->get_string("asr-backend");
    std::string cb = current_backend ? current_backend.c_str() : "whisper";
    int model_idx = 0;
    {
        auto models = asr ? asr->modelsForBackend(cb)
                          : std::vector<SherpaAsrAdapter::ModelInfo>{};
        auto model = Gtk::StringList::create({nullptr});
        for (size_t i = 0; i < models.size(); ++i) {
            if (saved_model && models[i].id == saved_model.c_str())
                model_idx = static_cast<int>(i);
            model->append(models[i].name.c_str());
        }
        model_row->set_model(model);
    }
    model_row->set_selected(static_cast<unsigned>(model_idx));
    auto* raw_model = reinterpret_cast<::AdwComboRow*>(
        static_cast<peel::Adw::ComboRow*>(model_row));
    g_signal_connect(raw_model, "notify::selected",
        G_CALLBACK(+[](::AdwComboRow* row, GParamSpec*, gpointer data) {
            auto* self = static_cast<VoiceSettingsPage*>(data);
            auto gs = Gio::Settings::create("io.github.fleischerdesign.Rook");
            auto* aa = dynamic_cast<SherpaAsrAdapter*>(self->m_stt);
            auto backend_str = gs->get_string("asr-backend");
            std::string be = backend_str ? backend_str.c_str() : "whisper";
            auto models = aa ? aa->modelsForBackend(be)
                             : std::vector<SherpaAsrAdapter::ModelInfo>{};
            guint sel = adw_combo_row_get_selected(row);
            if (sel < models.size())
                gs->set_string("asr-model",
                                      models[sel].id.c_str());
            gs->sync();
            self->rebuildEngineStatus();
            if (self->m_on_changed) self->m_on_changed();
        }), this);
    asr_list->append(std::move(model_row).release_floating_ptr());

    auto lang_row = Adw::ComboRow::create();
    lang_row->set_title(_("ASR Language"));
    lang_row->set_subtitle(_("Spoken language (not all backends support this)"));
    const char* langs[] = {"de", "en", "fr", "it", "es", "ja", "zh", "auto", nullptr};
    auto lang_model = Gtk::StringList::create(langs);
    adw_combo_row_set_model(
        reinterpret_cast<::AdwComboRow*>(static_cast<peel::Adw::ComboRow*>(lang_row)),
        G_LIST_MODEL(reinterpret_cast<::GtkStringList*>(
            static_cast<Gtk::StringList*>(lang_model))));
    auto saved_lang = settings->get_string("asr-language");
    int lang_idx = 0;
    if (saved_lang) {
        for (int i = 0; langs[i]; ++i)
            if (std::string(langs[i]) == saved_lang.c_str()) { lang_idx = i; break; }
    }
    lang_row->set_selected(static_cast<unsigned>(lang_idx));
    auto* raw_lang = reinterpret_cast<::AdwComboRow*>(
        static_cast<peel::Adw::ComboRow*>(lang_row));
    g_signal_connect(raw_lang, "notify::selected",
        reinterpret_cast<GCallback>(+[](::AdwComboRow* row, GParamSpec*, gpointer data) {
            auto* self = static_cast<VoiceSettingsPage*>(data);
            guint sel = adw_combo_row_get_selected(row);
            const char* lv[] = {"de","en","fr","it","es","ja","zh","auto",nullptr};
            if (sel < 8) {
                auto gs = Gio::Settings::create("io.github.fleischerdesign.Rook");
                gs->set_string("asr-language", lv[sel]);
                gs->sync();
            }
            self->rebuildEngineStatus();
            if (self->m_on_changed) self->m_on_changed();
        }), this);
    asr_list->append(std::move(lang_row).release_floating_ptr());

    auto thread_row = Adw::SpinRow::create_with_range(1.0, 16.0, 1.0);
    thread_row->set_title(_("CPU Threads"));
    thread_row->set_subtitle(_("Threads for speech recognition (1–16)"));
    thread_row->set_digits(0);
    auto* raw_th = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(thread_row));
    adw_spin_row_set_value(raw_th,
        static_cast<double>(settings->get_int("asr-threads")));
    g_signal_connect(raw_th, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = reinterpret_cast<peel::Gio::Settings*>(data);
            s->set_int( "asr-threads",
                static_cast<int>(adw_spin_row_get_value(row)));
            s->sync();
        }), settings_raw);
    asr_list->append(std::move(thread_row).release_floating_ptr());

    group.add(std::move(asr_list).release_floating_ptr());

    addSectionHeading(group, _("Text-to-Speech"), 16);

    auto tts_list = Gtk::ListBox::create();
    tts_list->add_css_class("boxed-list");
    tts_list->set_selection_mode(Gtk::SelectionMode::NONE);

    auto speed_row = Adw::SpinRow::create_with_range(0.5, 2.0, 0.05);
    speed_row->set_title(_("Speech Speed"));
    speed_row->set_subtitle(_("0.5 = half, 1.0 = normal, 2.0 = double speed"));
    speed_row->set_digits(2);
    auto* raw_sp = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(speed_row));
    adw_spin_row_set_value(raw_sp,
        settings->get_double("tts-speed"));
    g_signal_connect(raw_sp, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = reinterpret_cast<peel::Gio::Settings*>(data);
            s->set_double( "tts-speed",
                                  adw_spin_row_get_value(row));
            s->sync();
        }), settings_raw);
    tts_list->append(std::move(speed_row).release_floating_ptr());

    auto noise_row = Adw::SpinRow::create_with_range(0.0, 1.0, 0.05);
    noise_row->set_title(_("Voice Variation"));
    noise_row->set_subtitle(_("0.0 = monotone, 1.0 = most expressive"));
    noise_row->set_digits(2);
    auto* raw_nz = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(noise_row));
    adw_spin_row_set_value(raw_nz,
        settings->get_double("tts-noise-scale"));
    g_signal_connect(raw_nz, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = reinterpret_cast<peel::Gio::Settings*>(data);
            s->set_double( "tts-noise-scale",
                                  adw_spin_row_get_value(row));
            s->sync();
        }), settings_raw);
    tts_list->append(std::move(noise_row).release_floating_ptr());

    group.add(std::move(tts_list).release_floating_ptr());

    addSectionHeading(group, _("Audio Capture"), 16);

    auto capture_list = Gtk::ListBox::create();
    capture_list->add_css_class("boxed-list");
    capture_list->set_selection_mode(Gtk::SelectionMode::NONE);

    auto level_row = Adw::ActionRow::create();
    level_row->set_title(_("Input Level"));

    auto level_bar = Gtk::LevelBar::create();
    auto level_label = Gtk::Label::create("-60 dB");
    auto level_box = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 8);

    auto* raw_bar = reinterpret_cast<::GtkLevelBar*>(
        static_cast<peel::Gtk::LevelBar*>(level_bar));
    auto* raw_label = reinterpret_cast<::GtkLabel*>(
        static_cast<peel::Gtk::Label*>(level_label));

    level_bar->set_min_value(0.0);
    level_bar->set_max_value(1.0);
    level_bar->set_valign(Gtk::Align::CENTER);
    level_bar->set_hexpand(true);

    level_label->set_width_chars(6);
    level_label->set_xalign(1.0f);

    level_box->append(std::move(level_bar).release_floating_ptr());
    level_box->append(std::move(level_label).release_floating_ptr());

    level_row->add_suffix(
        reinterpret_cast<peel::Gtk::Widget*>(
            static_cast<peel::Gtk::Box*>(level_box)));
    capture_list->append(std::move(level_row).release_floating_ptr());

    m_level_timer_id = GLib::timeout_add(50,
        [device = m_audio_device, raw_bar, raw_label]() -> bool {
            float dbfs = device->level();
            float fraction = std::clamp((dbfs + 60.0f) / 60.0f, 0.0f, 1.0f);
            gtk_level_bar_set_value(raw_bar, fraction);
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f dB", dbfs);
            gtk_label_set_text(raw_label, buf);
            return true;
        });

    auto gain_row = Adw::SpinRow::create_with_range(0.5, 3.0, 0.1);
    gain_row->set_title(_("Mic Gain"));
    gain_row->set_subtitle(_("Amplify microphone input. 1.0 = no change"));
    gain_row->set_digits(1);
    auto* raw_gain = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(gain_row));
    adw_spin_row_set_value(raw_gain,
        settings->get_double("mic-gain"));
    g_signal_connect(raw_gain, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = reinterpret_cast<peel::Gio::Settings*>(data);
            s->set_double( "mic-gain",
                adw_spin_row_get_value(row));
            s->sync();
        }), settings_raw);
    capture_list->append(std::move(gain_row).release_floating_ptr());

    auto sil_row = Adw::SpinRow::create_with_range(0.0, 500.0, 10.0);
    sil_row->set_title(_("Silence Threshold"));
    sil_row->set_subtitle(_("Energy below this = silence. 0 = never skip, 80 = quiet room"));
    sil_row->set_digits(0);
    auto* raw_sil = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(sil_row));
    adw_spin_row_set_value(raw_sil,
        settings->get_double("voice-silence-threshold"));
    g_signal_connect(raw_sil, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = reinterpret_cast<peel::Gio::Settings*>(data);
            s->set_double( "voice-silence-threshold",
                adw_spin_row_get_value(row));
            s->sync();
        }), settings_raw);
    capture_list->append(std::move(sil_row).release_floating_ptr());

    auto barge_row = Adw::SpinRow::create_with_range(50.0, 5000.0, 50.0);
    barge_row->set_title(_("Barge-in Threshold"));
    barge_row->set_subtitle(_("Energy above this interrupts playback. 500 = normal, 200 = sensitive"));
    barge_row->set_digits(0);
    auto* raw_barge = reinterpret_cast<::AdwSpinRow*>(
        static_cast<peel::Adw::SpinRow*>(barge_row));
    adw_spin_row_set_value(raw_barge,
        settings->get_int("voice-barge-in-threshold"));
    g_signal_connect(raw_barge, "notify::value",
        G_CALLBACK(+[](::AdwSpinRow* row, GParamSpec*, gpointer data) {
            auto* s = reinterpret_cast<peel::Gio::Settings*>(data);
            s->set_int( "voice-barge-in-threshold",
                static_cast<int>(adw_spin_row_get_value(row)));
            s->sync();
        }), settings_raw);
    capture_list->append(std::move(barge_row).release_floating_ptr());

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
            if (!inputs[i].backend.empty())
                label = label + " [" + inputs[i].backend + "]";
            mic_model->append(label.c_str());
            if (inputs[i].is_default) mic_default_idx = static_cast<int>(i);
        }
        if (inputs.empty())
            mic_model->append(_("Default"));
        adw_combo_row_set_model(
            reinterpret_cast<::AdwComboRow*>(static_cast<peel::Adw::ComboRow*>(mic_row)),
            G_LIST_MODEL(reinterpret_cast<::GtkStringList*>(
                static_cast<Gtk::StringList*>(mic_model))));

        auto saved_mic = settings->get_string("microphone-device");
        if (saved_mic) {
            for (std::size_t i = 0; i < inputs.size(); ++i) {
                if (inputs[i].id == saved_mic.c_str()) {
                    mic_default_idx = static_cast<int>(i);
                    break;
                }
            }
        }
        mic_row->set_selected(static_cast<unsigned>(mic_default_idx));

        auto* raw_mic = reinterpret_cast<::AdwComboRow*>(
            static_cast<peel::Adw::ComboRow*>(mic_row));
        auto* dd_mic = new DeviceData{
            static_cast<peel::Gio::Settings*>(settings), inputs};
        g_signal_connect(raw_mic, "notify::selected",
            G_CALLBACK(+[](::AdwComboRow* row, GParamSpec*, gpointer data) {
                auto* d = static_cast<DeviceData*>(data);
                guint sel = adw_combo_row_get_selected(row);
                if (sel < d->devices.size()) {
                    d->settings->set_string("microphone-device",
                                          d->devices[sel].id.c_str());
                } else {
                    d->settings->set_string("microphone-device", "");
                }
                d->settings->sync();
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
            if (!outputs[i].backend.empty())
                label = label + " [" + outputs[i].backend + "]";
            spk_model->append(label.c_str());
            if (outputs[i].is_default) spk_default_idx = static_cast<int>(i);
        }
        if (outputs.empty())
            spk_model->append(_("Default"));
        adw_combo_row_set_model(
            reinterpret_cast<::AdwComboRow*>(static_cast<peel::Adw::ComboRow*>(spk_row)),
            G_LIST_MODEL(reinterpret_cast<::GtkStringList*>(
                static_cast<Gtk::StringList*>(spk_model))));

        auto saved_spk = settings->get_string("speaker-device");
        if (saved_spk) {
            for (std::size_t i = 0; i < outputs.size(); ++i) {
                if (outputs[i].id == saved_spk.c_str()) {
                    spk_default_idx = static_cast<int>(i);
                    break;
                }
            }
        }
        spk_row->set_selected(static_cast<unsigned>(spk_default_idx));

        auto* raw_spk = reinterpret_cast<::AdwComboRow*>(
            static_cast<peel::Adw::ComboRow*>(spk_row));
        auto* dd_spk = new DeviceData{
            static_cast<peel::Gio::Settings*>(settings), outputs};
        g_signal_connect(raw_spk, "notify::selected",
            G_CALLBACK(+[](::AdwComboRow* row, GParamSpec*, gpointer data) {
                auto* d = static_cast<DeviceData*>(data);
                guint sel = adw_combo_row_get_selected(row);
                if (sel < d->devices.size()) {
                    d->settings->set_string("speaker-device",
                                          d->devices[sel].id.c_str());
                } else {
                    d->settings->set_string("speaker-device", "");
                }
                d->settings->sync();
            }),
            dd_spk);
        (void)raw_spk;

        dev_list->append(std::move(spk_row).release_floating_ptr());
        group.add(std::move(dev_list).release_floating_ptr());
    }
}

} // namespace rook::gui
