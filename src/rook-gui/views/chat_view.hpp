#pragma once

#include <gtkmm.h>

namespace rook::gui {

class ChatView : public Gtk::Box {
public:
    ChatView();
    ~ChatView() override = default;

private:
    void setupUi();

    Gtk::ListBox m_message_list;
    Gtk::Entry m_input_entry;
    Gtk::Button m_send_button;
};

} // namespace rook::gui
