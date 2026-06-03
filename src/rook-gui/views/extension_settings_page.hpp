#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <string_view>
#include "rook/ports/extension_port.hpp"
#include "rook/adapters/mcp/mcp_server_manager.hpp"

namespace rook::gui {

class ExtensionSettingsPage : public peel::Gtk::Box
{
    PEEL_SIMPLE_CLASS(ExtensionSettingsPage, peel::Gtk::Box)

    static peel::Signal<ExtensionSettingsPage, void(void)> sig_changed;

    rook::ports::ExtensionPort *m_extensions = nullptr;
    rook::adapters::mcp::McpServerManager *m_mcp = nullptr;
    peel::Gtk::ListBox *m_list = nullptr;
    peel::Gtk::Stack *m_stack = nullptr;

    inline void init(Class *);

    void refreshList();
    void onInstallFromUrl();

public:
    PEEL_SIGNAL_CONNECT_METHOD(changed, sig_changed)

    static peel::FloatPtr<ExtensionSettingsPage> create(
        rook::ports::ExtensionPort *extensions,
        rook::adapters::mcp::McpServerManager *mcp);
};

} // namespace rook::gui
