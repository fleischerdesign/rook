#pragma once

#include <gtkmm.h>
#include <adwaita.h>
#include <memory>
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/domain/agent.hpp"
#include "rook/ports/llm_port.hpp"
#include "rook/ports/store_port.hpp"
#include "rook/adapters/secret_store.hpp"
#include "rook/core/settings.hpp"

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
    void on_startup() override;

private:
    void loadConfig();
    void saveConfig();
    void startModelDiscovery();

    std::string m_data_dir;
    rook::domain::EventBus m_bus;
    std::unique_ptr<rook::ports::LlmPort> m_llm;
    std::unique_ptr<rook::ports::StorePort> m_store;
    std::unique_ptr<rook::adapters::SecretStore> m_secrets;
    rook::core::SettingsLoader m_settings;
    rook::domain::ConversationManager m_conversations;
    std::unique_ptr<rook::domain::AgentEngine> m_engine;
    bool m_first_run = true;
};

} // namespace rook::gui
