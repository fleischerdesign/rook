#include "chat_sidebar.hpp"
#include <spdlog/spdlog.h>
#include <gtk/gtk.h> // gtk_orientable_set_orientation

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(ChatSidebar, "RookChatSidebar", Gtk::Box)

inline void ChatSidebar::Class::init()
{
    override_vfunc_dispose<ChatSidebar>();
}

inline void ChatSidebar::init(Class *)
{
    gtk_orientable_set_orientation(
        GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)),
        GTK_ORIENTATION_VERTICAL);
    set_size_request(220, -1);

    auto grid = Gtk::Grid::create();
    grid->set_row_homogeneous(true);
    grid->set_column_homogeneous(true);
    grid->set_row_spacing(6);
    grid->set_column_spacing(6);
    grid->set_margin_start(6);
    grid->set_margin_end(6);
    grid->set_margin_top(6);
    grid->set_margin_bottom(6);

    auto search_btn = Gtk::Button::create_from_icon_name("system-search-symbolic");
    search_btn->set_hexpand(true);
    search_btn->set_halign(Gtk::Align::FILL);
    search_btn->set_valign(Gtk::Align::FILL);
    grid->attach(std::move(search_btn), 0, 0, 1, 1);

    auto library_btn = Gtk::Button::create_from_icon_name("folder-documents-symbolic");
    library_btn->set_hexpand(true);
    library_btn->set_halign(Gtk::Align::FILL);
    library_btn->set_valign(Gtk::Align::FILL);
    grid->attach(std::move(library_btn), 1, 0, 1, 1);

    auto presets_btn = Gtk::Button::create_from_icon_name("document-edit-symbolic");
    presets_btn->set_hexpand(true);
    presets_btn->set_halign(Gtk::Align::FILL);
    presets_btn->set_valign(Gtk::Align::FILL);
    grid->attach(std::move(presets_btn), 0, 1, 1, 1);

    auto new_btn = Gtk::Button::create_from_icon_name("tab-new-symbolic");
    new_btn->set_hexpand(true);
    new_btn->set_halign(Gtk::Align::FILL);
    new_btn->set_valign(Gtk::Align::FILL);
    new_btn->add_css_class("suggested-action");
    new_btn->connect_clicked([this](Gtk::Button *) { onNewChat(nullptr); });
    grid->attach(std::move(new_btn), 1, 1, 1, 1);

    append(std::move(grid));

    auto scrolled = Gtk::ScrolledWindow::create();
    auto list = Gtk::ListBox::create();
    list->add_css_class("navigation-sidebar");
    list->set_hexpand(true);
    list->set_vexpand(true);
    list->connect_row_activated(
        [this](Gtk::ListBox *, Gtk::ListBoxRow *r) { onRowActivated(nullptr, r); });
    m_list = list;
    scrolled->set_child(std::move(list));
    append(std::move(scrolled));
}

inline void ChatSidebar::vfunc_dispose()
{
    if (m_bus) {
        m_bus->unsubscribe(m_created_handler);
        m_bus->unsubscribe(m_deleted_handler);
        m_bus->unsubscribe(m_updated_handler);
        m_bus->unsubscribe(m_selected_handler);
        m_bus = nullptr;
    }
    parent_vfunc_dispose<ChatSidebar>();
}

FloatPtr<ChatSidebar> ChatSidebar::create(rook::domain::EventBus &bus,
                                           rook::domain::ConversationManager &conv)
{
    auto sidebar = Object::create<ChatSidebar>();
    auto *s = static_cast<ChatSidebar*>(sidebar);
    s->m_bus = &bus;
    s->m_conv = &conv;

    s->m_created_handler = bus.subscribe<rook::domain::ChatCreated>(
        [s](const rook::domain::ChatCreated &event) { s->onChatCreated(event); });
    s->m_deleted_handler = bus.subscribe<rook::domain::ChatDeleted>(
        [s](const rook::domain::ChatDeleted &event) { s->onChatDeleted(event); });
    s->m_updated_handler = bus.subscribe<rook::domain::ChatUpdated>(
        [s](const rook::domain::ChatUpdated &event) { s->onChatUpdated(event); });
    s->m_selected_handler = bus.subscribe<rook::domain::ChatSelected>(
        [s](const rook::domain::ChatSelected &event) { s->onChatSelected(event); });

    return sidebar;
}

void ChatSidebar::onNewChat(Gtk::Button *)
{
    m_bus->publish(rook::domain::ChatSelected{.chat_id = ""});
}

void ChatSidebar::onRowActivated(Gtk::ListBox *, Gtk::ListBoxRow *row)
{
    if (!row) return;
    std::string chat_id(row->get_name());
    if (chat_id.empty()) return;
    m_conv->setActive(chat_id);
    m_bus->publish(rook::domain::ChatSelected{.chat_id = chat_id});
}

void ChatSidebar::onChatCreated(const rook::domain::ChatCreated &event)
{
    if (event.chat_id.empty()) return;
    auto conv = m_conv->open(event.chat_id);
    std::string title = event.chat_id;
    if (!conv.title.empty()) title = conv.title;
    std::string cid = event.chat_id;
    GLib::idle_add_once([this, id = std::move(cid), title = std::move(title)]() {
        addChatRow(id, title);
    });
}

void ChatSidebar::onChatDeleted(const rook::domain::ChatDeleted &event)
{
    GLib::idle_add_once([this, id = std::string(event.chat_id)]() {
        for (int i = 0; auto *row = m_list->get_row_at_index(i); ++i) {
            if (std::string(row->get_name()) == id) {
                m_list->remove(row);
                return;
            }
        }
    });
}

void ChatSidebar::onChatUpdated(const rook::domain::ChatUpdated &event)
{
    GLib::idle_add_once([this, id = event.chat_id, title = event.title]() {
        for (int i = 0; auto *row = m_list->get_row_at_index(i); ++i) {
            if (std::string(row->get_name()) == id) {
                auto *child = row->get_child();
                if (child) {
                    auto *label = child->template cast<Gtk::Label>();
                    if (label) {
                        auto display = title;
                        if (display.size() > 25) display = display.substr(0, 25) + "...";
                        label->set_text(display.c_str());
                    }
                }
                return;
            }
        }
    });
}

void ChatSidebar::onChatSelected(const rook::domain::ChatSelected &event)
{
    GLib::idle_add_once([this, id = event.chat_id]() {
        for (int i = 0; auto *row = m_list->get_row_at_index(i); ++i) {
            if (std::string(row->get_name()) == id) {
                m_list->select_row(row);
                return;
            }
        }
    });
}

void ChatSidebar::addChatRow(std::string_view id, std::string_view title)
{
    for (int i = 0; auto *existing = m_list->get_row_at_index(i); ++i) {
        if (std::string(existing->get_name()) == id) return;
    }

    auto display = std::string(title);
    if (display.size() > 25) display = display.substr(0, 25) + "...";

    auto row = Gtk::ListBoxRow::create();
    auto label = Gtk::Label::create(display.c_str());
    label->set_xalign(0.0f);
    label->set_margin_start(6);
    label->set_margin_end(6);
    label->set_margin_top(6);
    label->set_margin_bottom(6);
    label->set_max_width_chars(20);
    label->set_ellipsize(static_cast<Pango::EllipsizeMode>(PANGO_ELLIPSIZE_END));
    row->set_child(std::move(label).release_floating_ptr());
    row->set_name(std::string(id).c_str());
    m_list->append(std::move(row).release_floating_ptr());
}

void ChatSidebar::loadConversations(
    const std::vector<rook::domain::Conversation> &chats)
{
    while (auto *row = m_list->get_row_at_index(0)) {
        m_list->remove(row);
    }
    for (const auto &conv : chats) {
        auto display = conv.title.empty() ? conv.id : conv.title;
        addChatRow(conv.id, display);
    }
}

} // namespace rook::gui
