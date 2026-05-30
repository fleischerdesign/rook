#include "rook/domain/agent.hpp"
#include <spdlog/spdlog.h>

namespace rook::domain {

AgentEngine::AgentEngine(EventBus& bus, ports::LlmPort& llm, ConversationManager& conv)
    : m_bus(bus)
    , m_llm(llm)
    , m_conv(conv)
{}

void AgentEngine::start() {
    m_input_handler = m_bus.subscribe<UserInputReceived>(
        [this](const UserInputReceived& event) { onUserInput(event); });

    m_completed_handler = m_bus.subscribe<LlmCompleted>(
        [this](const LlmCompleted& event) { onLlmCompleted(event); });

    m_error_handler = m_bus.subscribe<LlmError>(
        [this](const LlmError& event) { onLlmError(event); });

    m_tool_handler = m_bus.subscribe<ToolCallCompleted>(
        [this](const ToolCallCompleted& event) { onToolCallCompleted(event); });

    spdlog::info("AgentEngine started");
}

void AgentEngine::onUserInput(const UserInputReceived& event) {
    spdlog::info("UserInput: chat={}, source={}", event.chat_id, event.source);

    ChatMessage msg;
    msg.role = "user";
    msg.content = event.content;
    m_conv.addMessage(event.chat_id, std::move(msg));

    auto messages = m_conv.buildLlmMessages(event.chat_id);

    m_bus.publish(LlmRequested{event.chat_id, ""});

    std::string chat_id = event.chat_id;

    m_llm.streamChat(
        event.chat_id,
        messages,
        [this, chat_id](std::string_view chunk, bool is_final) {
            if (!chunk.empty()) {
                m_bus.publish(LlmStreamChunk{
                    .chat_id = chat_id,
                    .content = std::string(chunk),
                    .is_final = is_final,
                });
            }

            if (is_final) {
                m_bus.publish(LlmCompleted{chat_id, 0});
            }
        }
    );
}

void AgentEngine::onLlmChunk(const LlmStreamChunk& chunk) {
    if (!chunk.chat_id.empty()) {
        m_conv.updateAssistantChunk(chunk.chat_id, chunk.content);
    }
}

void AgentEngine::onLlmCompleted(const LlmCompleted& /*event*/) {
    spdlog::info("LLM response complete");
}

void AgentEngine::onLlmError(const LlmError& event) {
    spdlog::error("LLM error in chat {}: {}", event.chat_id, event.message);
}

void AgentEngine::onToolCallCompleted(const ToolCallCompleted& /*event*/) {
    spdlog::info("Tool call completed");
}

} // namespace rook::domain
