#pragma once

#include <peel/Adw/Adw.h>
#include <peel/Gtk/Gtk.h>
#include <peel/Gio/Gio.h>
#include <peel/GLib/functions.h>
#include <peel/class.h>
#include <memory>
#include <string_view>
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/domain/agent.hpp"
#include "rook/ports/llm_port.hpp"
#include "rook/ports/store_port.hpp"
#include "rook/adapters/secret_store.hpp"
#include "rook/core/settings.hpp"
#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include "rook/adapters/mcp/mcp_client_adapter.hpp"
#include "rook/adapters/security/security_manager.hpp"
#include "rook/adapters/extension/extension_manager.hpp"
#include "rook/ports/extension_port.hpp"

namespace rook::gui {

class RookWindow;

class RookApplication final : public peel::Adw::Application
{
    PEEL_SIMPLE_CLASS(RookApplication, peel::Adw::Application)
    friend class peel::Gio::Application;

    inline void init(Class *);
    inline void vfunc_activate ();
    inline void vfunc_dispose ();

    void loadConfig();
    void saveConfig();
    void startModelDiscovery(RookWindow &window);
    void onFirstRunDone(RookWindow *window);

    std::string m_data_dir;
    rook::domain::EventBus m_bus;
    std::unique_ptr<rook::ports::LlmPort> m_llm;
    std::unique_ptr<rook::ports::StorePort> m_store;
    std::unique_ptr<rook::adapters::SecretStore> m_secrets;
    rook::core::SettingsLoader m_settings;
    rook::domain::ConversationManager m_conversations;
    std::unique_ptr<rook::domain::AgentEngine> m_engine;
    std::unique_ptr<rook::ports::ToolPort> m_tool_port;
    std::unique_ptr<rook::adapters::mcp::McpServerManager> m_mcp_manager;
    std::unique_ptr<rook::adapters::security::SecurityManager> m_security;
    std::unique_ptr<rook::adapters::extension::ExtensionManager> m_extensions;
    std::vector<rook::adapters::extension::CustomSkill> m_custom_skills;
    bool m_first_run = true;
    bool m_css_loaded = false;

public:
    static peel::RefPtr<RookApplication> create();

    rook::domain::EventBus& eventBus() { return m_bus; }
    rook::adapters::mcp::McpServerManager* mcpManager() { return m_mcp_manager.get(); }
    rook::adapters::security::SecurityManager* security() { return m_security.get(); }
    rook::adapters::extension::ExtensionManager* extensions() { return m_extensions.get(); }
    std::vector<rook::adapters::extension::CustomSkill>& customSkills() { return m_custom_skills; }
};

} // namespace rook::gui
