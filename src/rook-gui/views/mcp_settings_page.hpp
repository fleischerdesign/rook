#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <string_view>
#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include "rook/adapters/security/security_manager.hpp"

namespace rook::gui {

class McpSettingsPage : public peel::Gtk::Box
{
    PEEL_SIMPLE_CLASS(McpSettingsPage, peel::Gtk::Box)

    static peel::Signal<McpSettingsPage, void(void)> sig_changed;

    rook::adapters::mcp::McpServerManager *m_mcp = nullptr;
    rook::adapters::security::SecurityManager *m_security = nullptr;
    peel::Gtk::ListBox *m_list = nullptr;
    peel::Gtk::Stack *m_stack = nullptr;
    peel::Gtk::Label *m_mcp_header = nullptr;

    inline void init(Class *);

    void refreshList();
    void onAddServer();
    void onEditServer(std::string_view id);
    void onDeleteServer(std::string_view id);
    void onOverrideCapabilities(std::string_view id);
    bool isExtensionServer(std::string_view id) const;

public:
    PEEL_SIGNAL_CONNECT_METHOD(changed, sig_changed)

    static peel::FloatPtr<McpSettingsPage> create(
        rook::adapters::mcp::McpServerManager *mcp,
        rook::adapters::security::SecurityManager *security);
};

} // namespace rook::gui
