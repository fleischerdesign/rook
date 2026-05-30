#include "window.hpp"
#include "views/chat_view.hpp"
#include <iostream>

namespace rook::gui {

RookWindow::RookWindow() {
    set_title("Rook");
    set_default_size(900, 600);

    setupHeaderBar();
    setupStack();

    set_child(m_stack);
}

void RookWindow::setupHeaderBar() {
    m_header_bar.set_show_title_buttons(true);
    set_titlebar(m_header_bar);
}

void RookWindow::setupStack() {
    m_chat_view = std::make_unique<ChatView>();
    m_stack.add(*m_chat_view, "chat", "Chat");
    m_stack.set_visible_child("chat");
}

} // namespace rook::gui
