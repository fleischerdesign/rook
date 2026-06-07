#pragma once

#include <peel/Adw/Adw.h>
#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <functional>
#include <string_view>
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/ports/llm_port.hpp"
#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include "rook/adapters/security/security_manager.hpp"
#include "rook/ports/extension_port.hpp"
#include "rook/ports/tool_port.hpp"
#include "rook/adapters/extension/extension_manifest.hpp"

namespace rook::core { class DomainActor; }

namespace rook::gui {

class ChatSidebar;
class ChatView;

class RookWindow final : public peel::Adw::ApplicationWindow
{
    PEEL_SIMPLE_CLASS(RookWindow, peel::Adw::ApplicationWindow)

    peel::FloatPtr<peel::Adw::HeaderBar> m_header;
    peel::Adw::OverlaySplitView *m_split = nullptr;
    peel::Gtk::Button *m_sidebar_toggle = nullptr;
    ChatSidebar *m_sidebar = nullptr;
    ChatView *m_chat_view = nullptr;
    std::function<void()> m_save_fn;
    std::function<void(std::string_view)> m_before_uninstall_fn;
    rook::ports::LlmPort *m_llm = nullptr;
    rook::adapters::mcp::McpServerManager *m_mcp = nullptr;
    rook::adapters::security::SecurityManager *m_security = nullptr;
    rook::ports::ExtensionPort *m_extensions = nullptr;
    std::vector<rook::adapters::extension::CustomSkill> *m_custom_skills = nullptr;
    rook::ports::ToolPermissionPort *m_permission_port = nullptr;

    inline void init(Class *);

public:
    void showPreferences() { onPreferences(); }
    void showAbout() { onAbout(); }

    static RookWindow *create(peel::Gtk::Application *app,
                               rook::core::DomainActor *actor,
                               rook::domain::EventBus &bus,
                               rook::ports::LlmPort &llm,
                               rook::adapters::mcp::McpServerManager *mcp,
                               rook::adapters::security::SecurityManager *security,
                               rook::ports::ExtensionPort *extensions,
                               std::vector<rook::adapters::extension::CustomSkill> *custom_skills,
                               std::function<void()> save_fn,
                               std::function<void(std::string_view)> before_uninstall_fn = {},
                               rook::ports::ToolPermissionPort *permission_port = nullptr);

    void refreshModels();

private:
    void onPreferences();
    void onAbout();
};

} // namespace rook::gui
