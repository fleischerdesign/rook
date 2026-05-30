#pragma once

#include <gtkmm.h>
#include "rook/domain/events.hpp"
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"

namespace rook::gui {

class MessageWidget;

class ChatView : public Gtk::Box {
public:
    ChatView(rook::domain::EventBus& bus, rook::domain::ConversationManager& conv);
    ~ChatView() override;

    void setChatId(std::string_view id);

private:
    void setupUi();
    void onSendClicked();
    void onMessageEntryActivated();
    void onStreamChunk(const rook::domain::LlmStreamChunk& event);
    void onLlmCompleted(const rook::domain::LlmCompleted& event);
    void onChatSelected(const rook::domain::ChatSelected& event);
    void loadMessages(std::string_view chat_id);

    rook::domain::EventBus& m_bus;
    rook::domain::ConversationManager& m_conv;
    std::string m_chat_id;

    Gtk::ScrolledWindow m_scrolled;
    Gtk::ListBox m_message_list;
    Gtk::Entry m_input_entry;
    Gtk::Button m_send_button;
    MessageWidget* m_pending_assistant = nullptr;

    rook::domain::EventBus::HandlerId m_chunk_handler;
    rook::domain::EventBus::HandlerId m_completed_handler;
    rook::domain::EventBus::HandlerId m_chat_selected_handler;
};

} // namespace rook::gui
