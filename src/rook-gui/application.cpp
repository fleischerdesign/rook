#include "application.hpp"
#include "window.hpp"
#include "views/chat_view.hpp"
#include "views/chat_sidebar.hpp"
#include "rook/adapters/llm/llm_factory.hpp"

namespace rook::gui {

RookApplication::RookApplication()
    : Gtk::Application("io.github.fleischerdesign.Rook")
{
    m_llm = rook::adapters::llm::makeOllamaAdapter();
    m_llm->configure(rook::ports::LlmConfig{
        .provider = "ollama",
        .model = "llama3.1",
        .api_key = "",
        .base_url = "",
        .max_tokens = 4096,
        .temperature = 0.7f,
        .system_prompt = "",
    });

    m_conversations.start(m_bus);

    m_engine = std::make_unique<rook::domain::AgentEngine>(
        m_bus, *m_llm, m_conversations);

    m_engine->start();
}

Glib::RefPtr<RookApplication> RookApplication::create() {
    return Glib::make_refptr_for_instance<RookApplication>(new RookApplication());
}

void RookApplication::on_activate() {
    if (get_windows().empty()) {
        auto window = std::make_unique<RookWindow>(m_bus, *m_llm);
        add_window(*window);
        window->present();
        window.release();
    }
}

} // namespace rook::gui
