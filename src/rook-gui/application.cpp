#include "application.hpp"
#include "window.hpp"
#include "views/chat_view.hpp"
#include "views/chat_sidebar.hpp"
#include "views/first_run_wizard.hpp"
#include "rook/adapters/llm/multi_provider_adapter.hpp"
#include "rook/adapters/llm/llm_factory.hpp"
#include "rook/adapters/store/json_store.hpp"
#include "rook/adapters/model/model_cache.hpp"
#include "rook/adapters/model/model_discovery_factory.hpp"
#include <giomm.h>
#include <spdlog/spdlog.h>
#include <future>

namespace rook::gui {

RookApplication::RookApplication()
    : Gtk::Application("io.github.fleischerdesign.Rook")
{
    m_data_dir = Glib::get_user_data_dir() + "/rook";

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
}

Glib::RefPtr<RookApplication> RookApplication::create() {
    return Glib::make_refptr_for_instance<RookApplication>(new RookApplication());
}

void RookApplication::on_startup() {
    Gtk::Application::on_startup();
}

void RookApplication::on_activate() {
    if (!get_windows().empty()) return;

    auto save_fn = sigc::mem_fun(*this, &RookApplication::saveConfig);

    if (m_first_run) {
        auto* wizard = new FirstRunWizard();
        wizard->signal_done().connect([this, wizard, save_fn]() {
            auto cfg = wizard->getConfig();
            auto info = rook::ports::ProviderRegistry::instance().find(cfg.provider);

            auto new_provider = rook::ports::LlmProviderConfig{
                .id = "",
                .display_name = info ? info->display_name : cfg.provider,
                .type = cfg.provider,
                .base_url = info ? info->base_url : "",
                .api_key = cfg.api_key,
                .default_model = !cfg.model.empty() ? cfg.model
                    : (info ? info->default_model : ""),
                .enabled = true,
            };

            m_llm->addProvider(new_provider);
            m_first_run = false;
            saveConfig();
            wizard->close();
        });
        wizard->signal_hide().connect([wizard]() { delete wizard; });
        wizard->present();
    }

    auto window = std::make_unique<RookWindow>(m_bus, *m_llm, m_conversations, save_fn);
    add_window(*window);
    window->present();
    window.release();

    startModelDiscovery();
}

void RookApplication::startModelDiscovery() {
    auto providers = m_llm->listProviders();

    for (const auto& prov : providers) {
        if (!prov.enabled) continue;

        auto api_key = prov.api_key;
        auto base_url = prov.base_url;
        auto prov_type = prov.type;
        auto prov_id = prov.id;

        (void)std::async(std::launch::async, [prov_type, prov_id, api_key, base_url]() {
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
        });
    }
}

void RookApplication::loadConfig() {
    m_first_run = !m_settings.load(*m_store, *m_llm, *m_secrets);
}

void RookApplication::saveConfig() {
    m_settings.save(*m_store, *m_llm, *m_secrets);
    spdlog::info("Config saved to {}", m_data_dir);
}

} // namespace rook::gui
