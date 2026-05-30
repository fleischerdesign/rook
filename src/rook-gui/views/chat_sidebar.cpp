#include "chat_sidebar.hpp"
#include <spdlog/spdlog.h>

namespace rook::gui {

ChatSidebar::ChatSidebar(rook::domain::EventBus& bus)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0)
    , m_bus(bus)
{
    setupUi();

    m_created_handler = m_bus.subscribe<rook::domain::ChatSelected>(
        [this](const rook::domain::ChatSelected& event) {
            Glib::signal_idle().connect_once([this, id = event.chat_id]() {
                auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
                auto* label = Gtk::make_managed<Gtk::Label>(id);
                label->set_xalign(0.0f);
                label->set_margin(6);
                label->set_max_width_chars(20);
                label->set_ellipsize(Pango::EllipsizeMode::END);
                row->set_child(*label);
                row->set_name(id);
                m_list.append(*row);
            });
        });

    m_deleted_handler = m_bus.subscribe<rook::domain::ChatDeleted>(
        [this](const rook::domain::ChatDeleted& event) {
            onChatDeleted(event);
        });
}

ChatSidebar::~ChatSidebar() {
    m_bus.unsubscribe(m_created_handler);
    m_bus.unsubscribe(m_deleted_handler);
}

void ChatSidebar::setupUi() {
    set_size_request(220, -1);
    add_css_class("navigation-sidebar");

    auto header = Gtk::make_managed<Gtk::Label>("Chats");
    header->set_xalign(0.0f);
    header->set_margin(12);
    header->add_css_class("title-4");
    append(*header);

    m_new_button.set_label("New Chat");
    m_new_button.set_margin_start(12);
    m_new_button.set_margin_end(12);
    m_new_button.set_margin_bottom(6);
    m_new_button.set_halign(Gtk::Align::FILL);
    m_new_button.signal_clicked().connect(
        sigc::mem_fun(*this, &ChatSidebar::onNewChat));
    append(m_new_button);

    auto scrolled = Gtk::make_managed<Gtk::ScrolledWindow>();
    m_list.set_hexpand(true);
    m_list.set_vexpand(true);
    m_list.signal_row_activated().connect(
        sigc::mem_fun(*this, &ChatSidebar::onRowActivated));
    scrolled->set_child(m_list);
    append(*scrolled);
}

void ChatSidebar::onNewChat() {
    m_bus.publish(rook::domain::ChatCreated{
        .chat_id = ""
    });
}

void ChatSidebar::onRowActivated(Gtk::ListBoxRow* row) {
    if (!row) return;
    auto chat_id = row->get_name();
    if (chat_id.empty()) return;

    m_bus.publish(rook::domain::ChatSelected{
        .chat_id = chat_id
    });
}

void ChatSidebar::onChatDeleted(const rook::domain::ChatDeleted& event) {
    Glib::signal_idle().connect_once([this, id = event.chat_id]() {
        auto* row = m_list.get_row_at_index(0);
        for (int i = 0; row; ++i) {
            if (row->get_name() == id) {
                m_list.remove(*row);
                return;
            }
            row = m_list.get_row_at_index(i + 1);
        }
    });
}

void ChatSidebar::loadConversations(const std::vector<rook::domain::Conversation>& chats) {
    while (auto* row = m_list.get_row_at_index(0)) {
        m_list.remove(*row);
    }

    for (const auto& conv : chats) {
        auto display = conv.title.empty() ? conv.id : conv.title;
        if (display.size() > 25) display = display.substr(0, 25) + "...";

        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto* label = Gtk::make_managed<Gtk::Label>(display);
        label->set_xalign(0.0f);
        label->set_margin(6);
        label->set_max_width_chars(20);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        row->set_child(*label);
        row->set_name(conv.id);
        m_list.append(*row);
    }
}

} // namespace rook::gui
