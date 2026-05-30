#include "application.hpp"
#include "window.hpp"
#include "views/chat_view.hpp"
#include "views/chat_sidebar.hpp"
#include "views/first_run_wizard.hpp"
#include "rook/adapters/llm/multi_provider_adapter.hpp"
#include "rook/adapters/llm/llm_factory.hpp"
#include "rook/adapters/store/json_store.hpp"
#include <giomm.h>
#include <spdlog/spdlog.h>

namespace rook::gui {

RookApplication::RookApplication()
    : Gtk::Application("io.github.fleischerdesign.Rook")
{
    m_data_dir = Glib::get_user_data_dir() + "/rook";

    m_store = rook::adapters::store::makeJsonStore(m_data_dir);
    m_secrets = std::make_unique<rook::adapters::SecretStore>(
        "io.github.fleischerdesign.Rook");

    m_llm = rook::adapters::llm::makeMultiProviderAdapter();

    m_settings.load(*m_store, *m_llm, *m_secrets);

    m_llm->configure(rook::ports::LlmConfig{
        .provider = "ollama",
        .model = "llama3.1",
        .api_key = "",
        .base_url = "",
        .max_tokens = 4096,
        .temperature = 0.7f,
        .system_prompt = "You are Rook, a helpful AI assistant.",
    });

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
    if (get_windows().empty()) {
        auto save_fn = sigc::mem_fun(*this, &RookApplication::saveConfig);

        if (m_conversations.list().empty()) {
            auto* wizard = new FirstRunWizard();
            wizard->signal_done().connect([this, wizard, save_fn]() {
                auto cfg = wizard->getConfig();

                auto new_provider = rook::ports::LlmProviderConfig{
                    .id = "",
                    .display_name = cfg.provider == "ollama"   ? "Ollama (local)"
                                  : cfg.provider == "openai"   ? "OpenAI"
                                  : cfg.provider == "deepseek"  ? "DeepSeek"
                                  :                              "Anthropic",
                    .type = cfg.provider,
                    .base_url = cfg.provider == "openai"    ? "https://api.openai.com"
                              : cfg.provider == "deepseek"   ? "https://api.deepseek.com"
                              : cfg.provider == "anthropic"  ? "https://api.anthropic.com"
                              :                               "http://localhost:11434",
                    .api_key = cfg.api_key,
                    .default_model = cfg.model,
                    .enabled = true,
                    .is_default = true,
                };

                m_llm->addProvider(new_provider);
                m_llm->configure(cfg);
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
    }
}

void RookApplication::loadConfig() {
    m_settings.load(*m_store, *m_llm, *m_secrets);
}

void RookApplication::saveConfig() {
    m_settings.save(*m_store, *m_llm, *m_secrets);
    spdlog::info("Config saved to {}", m_data_dir);
}

} // namespace rook::gui
