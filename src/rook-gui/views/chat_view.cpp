#include "chat_view.hpp"

namespace rook::gui {

ChatView::ChatView()
    : Gtk::Box(Gtk::Orientation::VERTICAL, 6)
{
    setupUi();
}

void ChatView::setupUi() {
    m_message_list.set_hexpand(true);
    m_message_list.set_vexpand(true);
    m_message_list.add_css_class("navigation-sidebar");

    auto scrolled = Gtk::make_managed<Gtk::ScrolledWindow>();
    scrolled->set_child(m_message_list);
    scrolled->set_vexpand(true);

    append(*scrolled);

    auto input_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    input_box->set_margin(12);

    m_input_entry.set_hexpand(true);
    m_input_entry.set_placeholder_text("Type a message...");
    input_box->append(m_input_entry);

    m_send_button.set_label("Send");
    m_send_button.add_css_class("suggested-action");
    input_box->append(m_send_button);

    append(*input_box);
}

} // namespace rook::gui
