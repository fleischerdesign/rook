#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <functional>
#include <memory>
#include <string_view>
#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include "rook/adapters/security/security_manager.hpp"

namespace rook::gui {

class McpSettingsPage {
public:
    using ChangeFn = std::function<void()>;

    static std::unique_ptr<McpSettingsPage> create(
        rook::adapters::mcp::McpServerManager *mcp,
        rook::adapters::security::SecurityManager *security,
        ChangeFn on_changed);

    void populate(peel::Adw::PreferencesGroup &group);

private:
    McpSettingsPage() = default;

    rook::adapters::mcp::McpServerManager *m_mcp = nullptr;
    rook::adapters::security::SecurityManager *m_security = nullptr;
    peel::Gtk::ListBox *m_list = nullptr;
    peel::Gtk::Stack *m_stack = nullptr;
    peel::Gtk::Label *m_mcp_header = nullptr;
    ChangeFn m_on_changed;

    void refreshList();
    void onAddServer();
    void onEditServer(std::string_view id);
    void onDeleteServer(std::string_view id);
    void onOverrideCapabilities(std::string_view id);
    bool isExtensionServer(std::string_view id) const;
};

} // namespace rook::gui
