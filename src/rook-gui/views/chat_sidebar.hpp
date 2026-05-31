#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/GLib/functions.h>
#include <peel/class.h>
#include <string_view>
#include "rook/domain/events.hpp"
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"

namespace rook::gui {

class ChatSidebar final : public peel::Gtk::Box
{
    PEEL_SIMPLE_CLASS(ChatSidebar, peel::Gtk::Box)

    rook::domain::EventBus *m_bus = nullptr;
    rook::domain::ConversationManager *m_conv = nullptr;

    peel::FloatPtr<peel::Gtk::Button> m_new_button;
    peel::Gtk::ListBox *m_list = nullptr;

    rook::domain::EventBus::HandlerId m_created_handler;
    rook::domain::EventBus::HandlerId m_deleted_handler;
    rook::domain::EventBus::HandlerId m_updated_handler;
    rook::domain::EventBus::HandlerId m_selected_handler;

    inline void init(Class *);
    inline void vfunc_dispose ();

    void onNewChat(peel::Gtk::Button *);
    void onRowActivated(peel::Gtk::ListBox *, peel::Gtk::ListBoxRow *row);

    void onChatCreated(const rook::domain::ChatCreated &event);
    void onChatDeleted(const rook::domain::ChatDeleted &event);
    void onChatUpdated(const rook::domain::ChatUpdated &event);
    void onChatSelected(const rook::domain::ChatSelected &event);

    void addChatRow(std::string_view id, std::string_view title);

public:
    static peel::FloatPtr<ChatSidebar> create(rook::domain::EventBus &bus,
                                               rook::domain::ConversationManager &conv);

    void loadConversations(const std::vector<rook::domain::Conversation> &chats);
};

} // namespace rook::gui
