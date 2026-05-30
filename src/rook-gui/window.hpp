#pragma once

#include <gtkmm.h>
#include <adwaita.h>

namespace rook::gui {

class ChatView;

class RookWindow : public Gtk::Window {
public:
    RookWindow();
    ~RookWindow() override = default;

private:
    void setupHeaderBar();
    void setupStack();

    Gtk::HeaderBar m_header_bar;
    Gtk::Stack m_stack;
    std::unique_ptr<ChatView> m_chat_view;
};

} // namespace rook::gui
