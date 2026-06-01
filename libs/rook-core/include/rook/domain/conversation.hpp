#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>
#include "rook/ports/llm_port.hpp"
#include "rook/ports/store_port.hpp"
#include "rook/domain/event_bus.hpp"

namespace rook::domain {

struct ChatMessage {
    std::string id;
    std::string role;
    std::string content;
    std::string reasoning_content;
    std::chrono::system_clock::time_point timestamp;
    std::string tool_call_id;
    std::string tool_calls_json;
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
    void updateReasoningChunk(std::string_view conv_id, std::string_view chunk);
    void setTitle(std::string_view conv_id, std::string_view title);
    void setModel(std::string_view conv_id, std::string_view model);
    std::vector<ports::LlmMessage> buildLlmMessages(std::string_view conv_id) const;
    int32_t estimateTokens(std::string_view conv_id) const;

    std::optional<Conversation> active() const;
    void setActive(std::string_view id);

    void start(EventBus& bus, ports::StorePort* store = nullptr);
    void saveActiveConversation();
    void loadFromStore(ports::StorePort& store);

private:
    std::vector<Conversation> m_conversations;
    std::string m_active_id;

    EventBus* m_bus = nullptr;
    ports::StorePort* m_store = nullptr;
    EventBus::HandlerId m_chat_created_handler = 0;
    EventBus::HandlerId m_chat_deleted_handler = 0;

    std::string generateTitle(const Conversation& conv) const;
    void onChatCreated(const ChatCreated& event);
    void onChatDeleted(const ChatDeleted& event);
};

} // namespace rook::domain
