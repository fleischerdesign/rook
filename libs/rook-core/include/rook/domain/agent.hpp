#pragma once

#include <memory>
#include <string_view>
#include "rook/domain/events.hpp"
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/ports/llm_port.hpp"

namespace rook::domain {

class AgentEngine {
public:
    AgentEngine(EventBus& bus, ports::LlmPort& llm, ConversationManager& conv);

    void start();

private:
    EventBus& m_bus;
    ports::LlmPort& m_llm;
    ConversationManager& m_conv;

    void onUserInput(const UserInputReceived& event);
    void onLlmChunk(const LlmStreamChunk& chunk);
    void onLlmCompleted(const LlmCompleted& event);
    void onLlmError(const LlmError& event);
    void onToolCallCompleted(const ToolCallCompleted& event);

    EventBus::HandlerId m_input_handler;
    EventBus::HandlerId m_chunk_handler;
    EventBus::HandlerId m_completed_handler;
    EventBus::HandlerId m_error_handler;
    EventBus::HandlerId m_tool_handler;
};

} // namespace rook::domain
