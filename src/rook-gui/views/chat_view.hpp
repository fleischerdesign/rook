#pragma once

#include <gtkmm.h>
#include "rook/domain/events.hpp"
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class MessageWidget;

class ChatView : public Gtk::Box {
public:
    ChatView(rook::domain::EventBus& bus, rook::domain::ConversationManager& conv,
             rook::ports::LlmPort& llm);
    ~ChatView() override;

    void setChatId(std::string_view id);
    void populateModelDropdown();

private:
    struct InputBar {
        Gtk::Box box;
        Gtk::ComboBoxText model;
        Gtk::Entry entry;
        Gtk::Button send;
    };

    void setupUi();
    void onSendClicked();
    void onMessageEntryActivated();
    void onStreamChunk(const rook::domain::LlmStreamChunk& event);
    void onLlmCompleted(const rook::domain::LlmCompleted& event);
    void onChatSelected(const rook::domain::ChatSelected& event);
    void onChatDeleted(const rook::domain::ChatDeleted& event);
    void loadMessages(std::string_view chat_id);
    void setProcessing(bool active);
    void switchToChat(std::string_view chat_id);
    void doSend(InputBar& bar, std::string_view chat_id);

    rook::domain::EventBus& m_bus;
    rook::domain::ConversationManager& m_conv;
    rook::ports::LlmPort& m_llm;
    std::string m_chat_id;
    std::string m_pending_input;

    Gtk::Stack m_stack;
    Gtk::ScrolledWindow m_scrolled;
    Gtk::ListBox m_message_list;
    InputBar m_welcome_input;
    InputBar m_chat_input;
    MessageWidget* m_pending_assistant = nullptr;

    rook::domain::EventBus::HandlerId m_chunk_handler;
    rook::domain::EventBus::HandlerId m_locked_handler;
    rook::domain::EventBus::HandlerId m_unlock_handler;
    rook::domain::EventBus::HandlerId m_error_unlock_handler;
    rook::domain::EventBus::HandlerId m_chat_selected_handler;
    rook::domain::EventBus::HandlerId m_chat_deleted_handler;
    rook::domain::EventBus::HandlerId m_completed_handler;
};

} // namespace rook::gui
