#include "chat_sidebar.hpp"
#include <spdlog/spdlog.h>

namespace rook::gui {

ChatSidebar::ChatSidebar(rook::domain::EventBus& bus)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0)
    , m_bus(bus)
{
    setupUi();

    m_created_handler = m_bus.subscribe<rook::domain::ChatCreated>(
        [this](const rook::domain::ChatCreated& event) {
            onChatCreated(event);
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
    auto* label = dynamic_cast<Gtk::Label*>(row->get_child());
    if (!label) return;

    auto chat_id = label->get_name();
    m_bus.publish(rook::domain::ChatSelected{
        .chat_id = chat_id
    });
}

void ChatSidebar::onChatCreated(const rook::domain::ChatCreated& /*event*/) {
    Glib::signal_idle().connect_once([this]() {
        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto* label = Gtk::make_managed<Gtk::Label>("New Chat");
        label->set_xalign(0.0f);
        label->set_margin(6);
        row->set_child(*label);
        m_list.append(*row);
    });
}

void ChatSidebar::onChatDeleted(const rook::domain::ChatDeleted& /*event*/) {
    Glib::signal_idle().connect_once([this]() {
        auto* selected = m_list.get_selected_row();
        if (selected) m_list.remove(*selected);
    });
}

} // namespace rook::gui
