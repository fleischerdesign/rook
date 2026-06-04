#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>
#include <cstdint>
#include "rook/ports/llm_port.hpp"
#include "rook/ports/store_port.hpp"

namespace rook::domain {

struct ChatMessage {
    std::string id;
    std::string role;
    std::string content;
    std::string reasoning_content;
    std::chrono::system_clock::time_point timestamp;
    std::string tool_call_id;
    std::string tool_name;
    std::string tool_calls_json;
    bool has_tool_calls = false;
};

struct Conversation {
    std::string id;
    std::string title;
    std::string model;
    std::vector<ChatMessage> messages;
    std::vector<std::string> active_skill_ids;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    int32_t context_window = 8192;
    bool pinned = false;
    uint64_t pinned_at = 0;
    std::vector<std::string> whitelisted_tools;
    bool title_is_manual = false;
};

class ConversationManager {
public:
    Conversation open(std::string_view id);
    std::vector<Conversation> list() const;
    Conversation create(std::string_view title, std::string_view model);
    void remove(std::string_view id);
    void close(std::string_view id);

    void addMessage(std::string_view conv_id, ChatMessage message);
    void updateAssistantChunk(std::string_view conv_id, std::string_view chunk);
    void updateReasoningChunk(std::string_view conv_id, std::string_view chunk);
    void setAssistantToolCalls(std::string_view conv_id, std::string json);
    void setTitle(std::string_view conv_id, std::string_view title);
    void setModel(std::string_view conv_id, std::string_view model);
    void togglePin(std::string_view conv_id);
    bool isPinned(std::string_view conv_id) const;
    bool isToolWhitelisted(std::string_view conv_id, std::string_view tool) const;
    void addWhitelistedTool(std::string_view conv_id, std::string_view tool);
    std::vector<ports::LlmMessage> buildLlmMessages(std::string_view conv_id) const;
    int32_t estimateTokens(std::string_view conv_id) const;
    void setSystemMessage(std::string_view conv_id, std::string_view content);
    void updateSystemMessage(std::string_view conv_id, std::string_view content);

    void setActiveSkillIds(std::string_view conv_id,
                           std::vector<std::string> ids);
    std::vector<std::string> activeSkillIds(std::string_view conv_id) const;

    std::optional<Conversation> active() const;
    void setActive(std::string_view id);

    void setStore(ports::StorePort* store) { m_store = store; }
    void saveActiveConversation();
    void loadFromStore(ports::StorePort& store);

private:
    std::vector<Conversation> m_conversations;
    std::string m_active_id;

    ports::StorePort* m_store = nullptr;
};

} // namespace rook::domain
