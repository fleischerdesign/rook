#include "application.hpp"
#include "window.hpp"
#include "views/first_run_wizard.hpp"
#include "rook/adapters/llm/multi_provider_adapter.hpp"
#include "rook/adapters/llm/llm_factory.hpp"
#include "rook/adapters/store/json_store.hpp"
#include "rook/adapters/model/model_cache.hpp"
#include "rook/adapters/model/model_discovery_factory.hpp"
#include <spdlog/spdlog.h>
#include <future>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(RookApplication, "RookApplication", Adw::Application)

inline void RookApplication::Class::init()
{
    override_vfunc_activate<RookApplication>();
    override_vfunc_dispose<RookApplication>();
}

inline void RookApplication::init(Class *)
{
    new (&m_bus) rook::domain::EventBus();

    m_data_dir = GLib::get_user_data_dir() + std::string("/rook");

    m_store = rook::adapters::store::makeJsonStore(m_data_dir);
    m_secrets = std::make_unique<rook::adapters::SecretStore>(
        "io.github.fleischerdesign.Rook");

    m_llm = rook::adapters::llm::makeMultiProviderAdapter();

    m_first_run = !m_settings.load(*m_store, *m_llm, *m_secrets);

    m_conversations.start(m_bus, m_store.get());
    m_conversations.loadFromStore(*m_store);

    m_engine = std::make_unique<rook::domain::AgentEngine>(
        m_bus, *m_llm, m_conversations);
    m_engine->start();

    auto prefs_action = Gio::SimpleAction::create("preferences", nullptr);
    prefs_action->connect_activate(
        [this](Gio::SimpleAction *, GLib::Variant *) {
            if (auto *w = get_active_window())
                if (auto *rw = w->template cast<RookWindow>())
                    rw->showPreferences();
        });
    cast<Gio::ActionMap>()->add_action(prefs_action);

    auto about_action = Gio::SimpleAction::create("about", nullptr);
    about_action->connect_activate(
        [this](Gio::SimpleAction *, GLib::Variant *) {
            if (auto *w = get_active_window())
                if (auto *rw = w->template cast<RookWindow>())
                    rw->showAbout();
        });
    cast<Gio::ActionMap>()->add_action(about_action);
}

RefPtr<RookApplication> RookApplication::create()
{
    return Object::create<RookApplication>(
        prop_application_id(), "io.github.fleischerdesign.Rook",
        prop_flags(), Gio::Application::Flags::DEFAULT_FLAGS);
}

inline void RookApplication::vfunc_activate()
{
    parent_vfunc_activate<RookApplication>();
    if (get_active_window() != nullptr) return;

    auto *window = RookWindow::create(this, m_bus, *m_llm, m_conversations,
        [this]() { saveConfig(); });
    window->present();

    if (m_first_run) {
        auto wizard = FirstRunWizard::create();
        FirstRunWizard *raw_wiz = wizard;
        raw_wiz->connect_done([this, raw_wiz, window](FirstRunWizard *) {
            auto cfg = raw_wiz->getConfig();
            auto info = rook::ports::ProviderRegistry::instance().find(cfg.provider);

            auto new_provider = rook::ports::LlmProviderConfig{
                .id = "",
                .display_name = info ? info->display_name : cfg.provider,
                .type = cfg.provider,
                .base_url = info ? info->base_url : "",
                .api_key = cfg.api_key,
                .default_model = info ? info->default_model : "",
                .enabled = true,
            };

            m_llm->addProvider(new_provider);
            m_first_run = false;
            saveConfig();
            startModelDiscovery(*window);
            raw_wiz->close();
        });
        raw_wiz->connect_hide([raw_wiz](Gtk::Widget *) { g_object_unref(raw_wiz); });
        raw_wiz->present();
    }

    startModelDiscovery(*window);
}

inline void RookApplication::vfunc_dispose()
{
    m_llm.reset();
    m_store.reset();
    m_secrets.reset();
    m_engine.reset();
    m_bus.~EventBus();
    parent_vfunc_dispose<RookApplication>();
}

void RookApplication::startModelDiscovery(RookWindow &window)
{
    auto providers = m_llm->listProviders();
    RookWindow *win = &window;

    for (const auto &prov : providers) {
        if (!prov.enabled) continue;

        auto api_key = prov.api_key;
        auto base_url = prov.base_url;
        auto prov_type = prov.type;
        auto prov_id = prov.id;

        (void)std::async(std::launch::async, [prov_type, prov_id, api_key, base_url, win]() {
            std::unique_ptr<rook::ports::ModelDiscoveryPort> discovery;

            if (prov_type == "ollama") {
                discovery = rook::adapters::model::makeOllamaDiscovery(base_url);
            } else if (prov_type == "anthropic") {
                discovery = rook::adapters::model::makeAnthropicDiscovery();
            } else {
                discovery = rook::adapters::model::makeOpenAiDiscovery(base_url);
            }

            auto models = discovery->fetchModels(api_key);
            rook::adapters::model::ModelCache::instance().store(prov_id, std::move(models));

            GLib::idle_add_once([win]() {
                win->refreshModels();
            });
        });
    }
}

void RookApplication::saveConfig()
{
    m_settings.save(*m_store, *m_llm, *m_secrets);
    spdlog::info("Config saved to {}", m_data_dir);
}

} // namespace rook::gui
