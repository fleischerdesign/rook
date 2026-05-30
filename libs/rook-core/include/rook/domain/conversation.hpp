#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>
#include "rook/ports/llm_port.hpp"

namespace rook::domain {

struct ChatMessage {
    std::string id;
    std::string role; // "user", "assistant", "system", "tool"
    std::string content;
    std::chrono::system_clock::time_point timestamp;
    std::string tool_call_id;
    bool has_tool_calls = false;
};

struct Conversation {
    std::string id;
    std::string title;
    std::string model;
    std::vector<ChatMessage> messages;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    int32_t context_window = 8192;
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
    std::vector<ports::LlmMessage> buildLlmMessages(std::string_view conv_id) const;
    int32_t estimateTokens(std::string_view conv_id) const;

    std::optional<Conversation> active() const;
    void setActive(std::string_view id);

private:
    std::vector<Conversation> m_conversations;
    std::string m_active_id;

    std::string generateTitle(const Conversation& conv) const;
};

} // namespace rook::domain
