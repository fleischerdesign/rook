#include "window.hpp"
#include "views/chat_view.hpp"
#include "views/chat_sidebar.hpp"
#include "views/preferences_window.hpp"
#include <spdlog/spdlog.h>

namespace rook::gui {

RookWindow::RookWindow(rook::domain::EventBus& bus, rook::ports::LlmPort& llm,
                       rook::domain::ConversationManager& conversations,
                       sigc::slot<void()> on_settings_changed)
    : m_bus(bus)
    , m_llm(llm)
    , m_on_settings_changed(on_settings_changed)
{
    set_title("Rook");
    set_default_size(900, 600);

    setupHeaderBar();
    setupActions();
    setupLayout(conversations);

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

void RookWindow::setupActions() {
    m_action_group = Gio::SimpleActionGroup::create();

    m_action_group->add_action("settings",
        sigc::mem_fun(*this, &RookWindow::onSettings));

    m_action_group->add_action("about",
        sigc::mem_fun(*this, &RookWindow::onAbout));

    insert_action_group("win", m_action_group);
}

void RookWindow::onSettings() {
    auto* dialog = new PreferencesWindow(*this, m_llm, "");
    if (m_on_settings_changed) {
        dialog->signal_changed().connect(m_on_settings_changed);
    }
    dialog->signal_hide().connect([dialog]() { delete dialog; });
    dialog->present();
}

void RookWindow::onAbout() {
    auto* dialog = Gtk::make_managed<Gtk::AboutDialog>();
    dialog->set_transient_for(*this);
    dialog->set_program_name("Rook");
    dialog->set_version("0.1.0");
    dialog->set_comments("A multi-modal AI voice assistant");
    dialog->set_license_type(Gtk::License::MIT_X11);
    dialog->set_website("https://github.com/fleischerdesign/rook");
    dialog->set_website_label("GitHub");
    dialog->set_authors({"Philipp Fleischer"});
    dialog->set_modal(true);
    dialog->present();
}

void RookWindow::refreshModels() {
    if (m_chat_view) {
        m_chat_view->populateModelDropdown();
    }
}

void RookWindow::setupLayout(rook::domain::ConversationManager& conversations) {
    m_sidebar = Gtk::make_managed<ChatSidebar>(m_bus, conversations);
    m_chat_view = Gtk::make_managed<ChatView>(m_bus, conversations, m_llm);

    m_sidebar->loadConversations(conversations.list());

    m_paned.set_start_child(*m_sidebar);
    m_paned.set_end_child(*m_chat_view);
    m_paned.set_position(220);
    m_paned.set_resize_start_child(false);
    m_paned.set_shrink_start_child(false);
}

} // namespace rook::gui
