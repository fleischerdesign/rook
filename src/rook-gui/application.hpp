#pragma once

#include <gtkmm.h>
#include <adwaita.h>
#include <memory>
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/domain/agent.hpp"
#include "rook/ports/llm_port.hpp"
#include "rook/ports/store_port.hpp"

namespace rook::gui {

class RookWindow;

class RookApplication : public Gtk::Application {
public:
    static Glib::RefPtr<RookApplication> create();
    ~RookApplication() override = default;

    rook::domain::EventBus& eventBus() { return m_bus; }

protected:
    RookApplication();
    void on_activate() override;

private:
    rook::domain::EventBus m_bus;
    std::unique_ptr<rook::ports::LlmPort> m_llm;
    rook::domain::ConversationManager m_conversations;
    std::unique_ptr<rook::domain::AgentEngine> m_engine;
};

} // namespace rook::gui
