#pragma once

#include <memory>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <future>
#include "rook/domain/events.hpp"
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/ports/llm_port.hpp"
#include "rook/ports/tool_port.hpp"

namespace rook::adapters::extension {
struct CustomSkill;
}

namespace rook::ports {
class ExtensionPort;
}

namespace rook::domain {

class AgentEngine {
public:
    AgentEngine(EventBus& bus, ports::LlmPort& llm,
                ConversationManager& conv, ports::ToolPort& tool,
                ports::ExtensionPort* extensions = nullptr,
                std::vector<rook::adapters::extension::CustomSkill>* custom_skills = nullptr,
                ports::ToolPermissionPort* permission_port = nullptr);

    void start();

private:
    EventBus& m_bus;
    ports::LlmPort& m_llm;
    ConversationManager& m_conv;
    ports::ToolPort& m_tool;
    ports::ExtensionPort* m_extensions = nullptr;
    std::vector<rook::adapters::extension::CustomSkill>* m_custom_skills = nullptr;

    void onUserInput(const UserInputReceived& event);
    void onLlmChunk(const LlmStreamChunk& chunk);
    void onLlmCompleted(const LlmCompleted& event);
    void onLlmError(const LlmError& event);
    void onToolCallCompleted(const ToolCallCompleted& event);
    void onPermissionDecision(const ToolCallPermissionDecision& event);
    void onPermissionTimeout(const ToolCallTimedOut& event);
    void onSkillToggled(const SkillToggled& event);
    void onChatSelected(const ChatSelected& event);

    void runLlm(std::string chat_id, std::string model);
    bool processPendingToolCalls(std::string_view chat_id);
    void executeSingleTool(std::string_view chat_id, const ports::ToolCall& call);
    void injectSkillsOnFirstMessage(std::string_view chat_id);
    void syncAlwaysOnSkills(std::string_view chat_id);
    void rebuildSystemMessage(std::string_view chat_id);
    std::string buildSystemPrompt(std::string_view chat_id);

    struct PendingPermission {
        std::string chat_id;
        std::string model;
        std::vector<ports::ToolCall> calls;
    };

    std::vector<ports::ToolCall> m_pending_tool_calls;
    std::unordered_map<std::string, PendingPermission> m_pending_permissions;
    std::future<void> m_title_future;

    ports::ToolPermissionPort* m_permission_port = nullptr;

    EventBus::HandlerId m_input_handler;
    EventBus::HandlerId m_chunk_handler;
    EventBus::HandlerId m_completed_handler;
    EventBus::HandlerId m_error_handler;
    EventBus::HandlerId m_tool_handler;
    EventBus::HandlerId m_skill_handler;
    EventBus::HandlerId m_chat_selected_handler;
    EventBus::HandlerId m_perm_decision_handler;
    EventBus::HandlerId m_perm_timeout_handler;
};

} // namespace rook::domain
