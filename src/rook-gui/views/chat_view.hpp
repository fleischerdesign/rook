#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <peel/GLib/functions.h>
#include <peel/class.h>
#include <string>
#include <string_view>
#include <vector>
#include "rook/domain/events.hpp"
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class MessageWidget;

class ChatView final : public peel::Gtk::Box
{
    PEEL_SIMPLE_CLASS(ChatView, peel::Gtk::Box)

    rook::domain::EventBus *m_bus = nullptr;
    rook::domain::ConversationManager *m_conv = nullptr;
    rook::ports::LlmPort *m_llm = nullptr;

    std::string m_chat_id;
    std::string m_pending_input;

    peel::FloatPtr<peel::Gtk::Stack> m_stack;
    peel::FloatPtr<peel::Gtk::ScrolledWindow> m_scrolled;
    peel::FloatPtr<peel::Gtk::ListBox> m_message_list;

    peel::Gtk::DropDown *m_welcome_model = nullptr;
    peel::Gtk::DropDown *m_chat_model = nullptr;
    peel::Gtk::Entry *m_welcome_entry = nullptr;
    peel::Gtk::Entry *m_chat_entry = nullptr;
    peel::Gtk::Button *m_welcome_send = nullptr;
    peel::Gtk::Button *m_chat_send = nullptr;

    std::vector<std::string> m_welcome_ids;
    std::vector<std::string> m_chat_ids;

    MessageWidget *m_pending_assistant = nullptr;

    rook::domain::EventBus::HandlerId m_chunk_handler;
    rook::domain::EventBus::HandlerId m_locked_handler;
    rook::domain::EventBus::HandlerId m_unlock_handler;
    rook::domain::EventBus::HandlerId m_error_unlock_handler;
    rook::domain::EventBus::HandlerId m_chat_selected_handler;
    rook::domain::EventBus::HandlerId m_chat_deleted_handler;
    rook::domain::EventBus::HandlerId m_completed_handler;

    inline void init(Class *);
    inline void vfunc_dispose ();

    void onSendClicked(peel::Gtk::Button *);
    void onMessageEntryActivated(peel::Gtk::Entry *);
    void onStreamChunk(const rook::domain::LlmStreamChunk &event);
    void onLlmCompleted(const rook::domain::LlmCompleted &event);
    void onChatSelected(const rook::domain::ChatSelected &event);
    void onChatDeleted(const rook::domain::ChatDeleted &event);
    void loadMessages(std::string_view chat_id);
    void setProcessing(bool active);
    void switchToChat(std::string_view chat_id);

    void doSend(std::string_view chat_id);
    void createInputBar(
        peel::Gtk::DropDown *&model_out,
        peel::Gtk::Entry *&entry_out,
        peel::Gtk::Button *&button_out,
        std::vector<std::string> &ids_out);

public:
    static peel::FloatPtr<ChatView> create(rook::domain::EventBus &bus,
                                             rook::domain::ConversationManager &conv,
                                             rook::ports::LlmPort &llm);

    void populateModelDropdown();
    void setChatId(std::string_view id);
};

} // namespace rook::gui
