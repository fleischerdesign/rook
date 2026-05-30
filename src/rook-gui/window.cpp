#include "window.hpp"
#include "views/chat_view.hpp"
#include "views/chat_sidebar.hpp"
#include <spdlog/spdlog.h>

namespace rook::gui {

RookWindow::RookWindow(rook::domain::EventBus& bus)
    : m_bus(bus)
{
    set_title("Rook");
    set_default_size(900, 600);

    setupHeaderBar();
    setupLayout();

    set_child(m_paned);
}

void RookWindow::setupHeaderBar() {
    m_header_bar.set_show_title_buttons(true);
    set_titlebar(m_header_bar);

    auto* menu_button = Gtk::make_managed<Gtk::MenuButton>();
    menu_button->set_icon_name("open-menu-symbolic");

    auto menu = Gio::Menu::create();
    menu->append("Settings", "win.settings");
    menu->append("About", "win.about");

    menu_button->set_menu_model(menu);
    m_header_bar.pack_end(*menu_button);
}

void RookWindow::setupLayout() {
    m_sidebar = Gtk::make_managed<ChatSidebar>(m_bus);
    m_chat_view = Gtk::make_managed<ChatView>(m_bus);

    m_paned.set_start_child(*m_sidebar);
    m_paned.set_end_child(*m_chat_view);
    m_paned.set_position(220);
    m_paned.set_resize_start_child(false);
    m_paned.set_shrink_start_child(false);
}

} // namespace rook::gui
