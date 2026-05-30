#pragma once

#include <gtkmm.h>
#include <adwaita.h>
#include "rook/domain/event_bus.hpp"

namespace rook::gui {

class ChatView;
class ChatSidebar;
class MessageWidget;

class RookWindow : public Gtk::Window {
public:
    explicit RookWindow(rook::domain::EventBus& bus);
    ~RookWindow() override = default;

private:
    void setupHeaderBar();
    void setupLayout();

    rook::domain::EventBus& m_bus;
    Gtk::HeaderBar m_header_bar;
    Gtk::Paned m_paned;
    ChatSidebar* m_sidebar = nullptr;
    ChatView* m_chat_view = nullptr;
};

} // namespace rook::gui
