#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <peel/GLib/functions.h>
#include <peel/class.h>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include "rook/domain/events.hpp"
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/ports/llm_port.hpp"
#include "rook/adapters/extension/extension_manifest.hpp"

namespace rook::core { class DomainActor; }

namespace rook::ports {
class ExtensionPort;
class ToolPermissionPort;
}

namespace rook::gui {

class MessageWidget;
class PermissionBanner;

class ChatView final : public peel::Gtk::Box
{
    PEEL_SIMPLE_CLASS(ChatView, peel::Gtk::Box)

    rook::core::DomainActor *m_actor = nullptr;
    rook::domain::EventBus *m_bus = nullptr;
    rook::domain::ConversationManager *m_conv = nullptr;
    rook::ports::LlmPort *m_llm = nullptr;
    rook::ports::ExtensionPort *m_extensions = nullptr;
    std::vector<rook::adapters::extension::CustomSkill> *m_custom_skills = nullptr;

    std::string m_chat_id;
    std::string m_pending_input;

    peel::Gtk::Stack *m_stack = nullptr;
    peel::Gtk::ScrolledWindow *m_scrolled = nullptr;
    peel::Gtk::ListBox *m_message_list = nullptr;

    peel::Gtk::DropDown *m_welcome_model = nullptr;
    peel::Gtk::DropDown *m_chat_model = nullptr;
    peel::Gtk::Entry *m_welcome_entry = nullptr;
    peel::Gtk::Entry *m_chat_entry = nullptr;
    peel::Gtk::Button *m_welcome_send = nullptr;
    peel::Gtk::Button *m_chat_send = nullptr;

    std::vector<std::string> m_welcome_ids;
    std::vector<std::string> m_chat_ids;
    std::vector<std::string> m_welcome_pending_skills;

    MessageWidget *m_pending_assistant = nullptr;
    std::map<std::string, peel::Gtk::Widget*> m_pending_tool_rows;

    rook::domain::EventBus::HandlerId m_chunk_handler;
    rook::domain::EventBus::HandlerId m_locked_handler;
    rook::domain::EventBus::HandlerId m_unlock_handler;
    rook::domain::EventBus::HandlerId m_error_unlock_handler;
    rook::domain::EventBus::HandlerId m_chat_selected_handler;
    rook::domain::EventBus::HandlerId m_chat_deleted_handler;
    rook::domain::EventBus::HandlerId m_completed_handler;
    rook::domain::EventBus::HandlerId m_tool_requested_handler;
    rook::domain::EventBus::HandlerId m_tool_completed_handler;

    rook::domain::EventBus::HandlerId m_skill_handler;

    rook::domain::EventBus::HandlerId m_perm_request_handler;
    rook::domain::EventBus::HandlerId m_perm_timeout_handler;

    peel::Gtk::MenuButton *m_skills_btn = nullptr;
    peel::Gtk::MenuButton *m_welcome_skills_btn = nullptr;
    peel::Gtk::Popover *m_skills_popover = nullptr;
    peel::FloatPtr<peel::Gtk::Popover> m_command_popover;
    peel::Gtk::ListBox *m_command_listbox = nullptr;

    peel::Gtk::Box *m_banner_slot = nullptr;
    PermissionBanner *m_active_banner = nullptr;
    rook::ports::ToolPermissionPort *m_permission_port = nullptr;
    guint m_banner_timeout_id = 0;

    inline void init(Class *);
    inline void vfunc_dispose ();

    void onSendClicked(peel::Gtk::Button *);
    void onMessageEntryActivated(peel::Gtk::Entry *);
    void onStreamChunk(const rook::domain::LlmStreamChunk &event);
    void onLlmCompleted(const rook::domain::LlmCompleted &event);
    void onToolCallRequested(const rook::domain::ToolCallRequested &event);
    void onToolCallCompleted(const rook::domain::ToolCallCompleted &event);
    void onChatSelected(const rook::domain::ChatSelected &event);
    void onChatDeleted(const rook::domain::ChatDeleted &event);
    void onPermissionRequest(const rook::domain::ToolCallPermissionRequest &event);
    void onPermissionTimeout(const rook::domain::ToolCallTimedOut &event);
    void loadMessages(std::string_view chat_id);
    void setProcessing(bool active);
    void switchToChat(std::string_view chat_id);

    void doSend(std::string_view chat_id);
    void createInputBar(
        peel::Gtk::DropDown *&model_out,
        peel::Gtk::Entry *&entry_out,
        peel::Gtk::Button *&button_out,
        std::vector<std::string> &ids_out);
    void buildSkillsPopover(peel::Gtk::MenuButton *target = nullptr);
    void onChatEntryChanged();

public:
    static peel::FloatPtr<ChatView> create(rook::core::DomainActor *actor,
                                               rook::domain::EventBus &bus,
                                               rook::domain::ConversationManager &conv,
                                               rook::ports::LlmPort &llm,
                                               rook::ports::ExtensionPort *extensions = nullptr,
                                               std::vector<rook::adapters::extension::CustomSkill> *custom_skills = nullptr,
                                               rook::ports::ToolPermissionPort *permission_port = nullptr);

    void setConv(rook::domain::ConversationManager *conv) { m_conv = conv; }

    void populateModelDropdown();
    void setChatId(std::string_view id);
};

} // namespace rook::gui
