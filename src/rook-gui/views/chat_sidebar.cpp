#include "chat_sidebar.hpp"
#include <spdlog/spdlog.h>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(ChatSidebar, "RookChatSidebar", Gtk::Box)

inline void ChatSidebar::Class::init()
{
    override_vfunc_dispose<ChatSidebar>();
}

inline void ChatSidebar::init(Class *)
{
    set_size_request(220, -1);
    add_css_class("navigation-sidebar");

    auto header = Gtk::Label::create("Chats");
    header->set_xalign(0.0f);
    header->set_margin_start(12);
    header->set_margin_end(12);
    header->set_margin_top(12);
    header->set_margin_bottom(12);
    header->add_css_class("title-4");
    append(std::move(header));

    m_new_button = Gtk::Button::create_with_label("New Chat");
    m_new_button->set_margin_start(12);
    m_new_button->set_margin_end(12);
    m_new_button->set_margin_bottom(6);
    m_new_button->set_halign(Gtk::Align::FILL);
    m_new_button->connect_clicked([this](Gtk::Button *) { onNewChat(nullptr); });
    append(std::move(m_new_button));

    auto scrolled = Gtk::ScrolledWindow::create();
    m_list = Gtk::ListBox::create();
    m_list->set_hexpand(true);
    m_list->set_vexpand(true);
    m_list->connect_row_activated(
        [this](Gtk::ListBox *, Gtk::ListBoxRow *r) { onRowActivated(nullptr, r); });
    scrolled->set_child(std::move(m_list));
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
