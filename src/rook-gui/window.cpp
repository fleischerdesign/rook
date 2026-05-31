#include "window.hpp"
#include "views/chat_view.hpp"
#include "views/chat_sidebar.hpp"

#include <peel/Gio/Gio.h>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(RookWindow, "RookWindow", Adw::ApplicationWindow)

inline void RookWindow::Class::init()
{
}

inline void RookWindow::init(Class *)
{
    set_title("Rook");
    set_default_size(900, 650);
}

RookWindow *RookWindow::create(Gtk::Application *app,
                                rook::domain::EventBus &bus,
                                rook::ports::LlmPort &llm,
                                rook::domain::ConversationManager &conv,
                                std::function<void()> save_fn)
{
    auto *win = Object::create<RookWindow>(prop_application(), app);
    win->m_save_fn = std::move(save_fn);

    auto sidebar = ChatSidebar::create(bus, conv);
    sidebar->loadConversations(conv.list());
    win->m_sidebar = std::move(sidebar).release_floating_ptr();

    auto chat = ChatView::create(bus, conv, llm);
    win->m_chat_view = std::move(chat).release_floating_ptr();

    win->m_header = Adw::HeaderBar::create();

    RefPtr<Gio::Menu> menu = Gio::Menu::create();
    menu->append("_Preferences", "app.preferences");
    menu->append("_About", "app.about");
    auto popover = Gtk::PopoverMenu::create_from_model(menu);

    auto menu_button = Gtk::MenuButton::create();
    menu_button->set_icon_name("open-menu-symbolic");
    menu_button->set_popover(std::move(popover));
    menu_button->set_tooltip_text("Menu");
    win->m_header->pack_end(std::move(menu_button));

    auto sidebar_page = Adw::NavigationPage::create(
        win->m_sidebar, "Chats");
    auto content_page = Adw::NavigationPage::create(
        win->m_chat_view, "");

    win->m_split = Adw::NavigationSplitView::create();
    win->m_split->set_sidebar(std::move(sidebar_page).release_floating_ptr());
    win->m_split->set_content(std::move(content_page).release_floating_ptr());

    auto toolbar = Adw::ToolbarView::create();
    toolbar->add_top_bar(std::move(win->m_header));
    toolbar->set_content(std::move(win->m_split));

    win->set_content(std::move(toolbar));

    return win;
}

void RookWindow::refreshModels()
{
    if (m_chat_view) m_chat_view->populateModelDropdown();
}

void RookWindow::onPreferences()
{
    // TODO: Adw::PreferencesDialog with settings pages
}

void RookWindow::onAbout()
{
    const char *developers[] = {
        "Philipp Fleischer <hello@fleischerdesign.de>",
        nullptr
    };
    Adw::show_about_dialog(this,
        Adw::AboutDialog::prop_application_name(), "Rook",
        Adw::AboutDialog::prop_application_icon(), "application-x-executable",
        Adw::AboutDialog::prop_version(), "0.1.0",
        Adw::AboutDialog::prop_license_type(), Gtk::License::MIT_X11,
        Adw::AboutDialog::prop_comments(),
            "Multi-modal AI assistant with wake-word voice control.",
        Adw::AboutDialog::prop_developers(), developers);
}

} // namespace rook::gui
