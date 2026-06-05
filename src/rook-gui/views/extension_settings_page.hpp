#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <functional>
#include <memory>
#include <string_view>
#include "rook/ports/extension_port.hpp"
#include "rook/adapters/mcp/mcp_server_manager.hpp"

namespace rook::gui {

class ExtensionSettingsPage {
public:
    using ChangeFn = std::function<void()>;

    static std::unique_ptr<ExtensionSettingsPage> create(
        rook::ports::ExtensionPort *extensions,
        rook::adapters::mcp::McpServerManager *mcp,
        ChangeFn on_changed);

    void populate(peel::Adw::PreferencesGroup &group);

private:
    ExtensionSettingsPage() = default;

    rook::ports::ExtensionPort *m_extensions = nullptr;
    rook::adapters::mcp::McpServerManager *m_mcp = nullptr;
    peel::Gtk::ListBox *m_list = nullptr;
    peel::Gtk::Stack *m_stack = nullptr;
    ChangeFn m_on_changed;

    void refreshList();
    void onInstallFromUrl();
};

} // namespace rook::gui
