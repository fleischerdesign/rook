#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <functional>
#include <memory>
#include <string_view>
#include "rook/ports/extension_port.hpp"
#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include "rook/adapters/hook/hook_registry.hpp"

namespace rook::gui {

class ExtensionSettingsPage {
public:
    using ChangeFn = std::function<void()>;
    using BeforeUninstallFn = std::function<void(std::string_view extension_name)>;

    static std::unique_ptr<ExtensionSettingsPage> create(
        rook::ports::ExtensionPort *extensions,
        rook::adapters::mcp::McpServerManager *mcp,
        ChangeFn on_changed,
        BeforeUninstallFn on_before_uninstall = {});

    void populate(peel::Adw::PreferencesGroup &group);

private:
    ExtensionSettingsPage() = default;

    rook::ports::ExtensionPort *m_extensions = nullptr;
    rook::adapters::mcp::McpServerManager *m_mcp = nullptr;
    peel::Gtk::ListBox *m_list = nullptr;
    peel::Gtk::Stack *m_stack = nullptr;
    ChangeFn m_on_changed;
    BeforeUninstallFn m_on_before_uninstall;

    void refreshList();
    void onInstallFromUrl();
};

} // namespace rook::gui
