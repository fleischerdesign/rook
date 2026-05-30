#pragma once

#include <gtkmm.h>
#include <vector>
#include "rook/domain/events.hpp"
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"

namespace rook::gui {

class ChatSidebar : public Gtk::Box {
public:
    ChatSidebar(rook::domain::EventBus& bus);
    ~ChatSidebar() override;

    void loadConversations(const std::vector<rook::domain::Conversation>& chats);

private:
    void setupUi();
    void onNewChat();
    void onRowActivated(Gtk::ListBoxRow* row);
    void onChatCreated(const rook::domain::ChatCreated& event);
    void onChatDeleted(const rook::domain::ChatDeleted& event);

    rook::domain::EventBus& m_bus;
    Gtk::ListBox m_list;
    Gtk::Button m_new_button;

    rook::domain::EventBus::HandlerId m_created_handler;
    rook::domain::EventBus::HandlerId m_deleted_handler;
};

} // namespace rook::gui
