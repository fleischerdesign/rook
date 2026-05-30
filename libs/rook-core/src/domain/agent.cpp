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

    m_chunk_handler = m_bus.subscribe<LlmStreamChunk>(
        [this](const LlmStreamChunk& event) { onLlmChunk(event); });

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

    m_conv.setModel(event.chat_id, event.model);

    auto messages = m_conv.buildLlmMessages(event.chat_id);

    m_bus.publish(LlmRequested{event.chat_id, ""});

    std::string chat_id = event.chat_id;
    std::string model = event.model;

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
        },
        model
    );
}

void AgentEngine::onLlmChunk(const LlmStreamChunk& chunk) {
    if (!chunk.chat_id.empty() && !chunk.chat_id.starts_with("__title__")) {
        m_conv.updateAssistantChunk(chunk.chat_id, chunk.content);
    }
}

void AgentEngine::onLlmCompleted(const LlmCompleted& event) {
    spdlog::info("LLM response complete for chat {}", event.chat_id);

    auto conv = m_conv.open(event.chat_id);
    if (conv.title != "New Chat") return;

    int user_count = 0;
    int assistant_count = 0;
    std::string last_user;
    std::string last_assistant;

    for (const auto& msg : conv.messages) {
        if (msg.role == "user") { user_count++; last_user = msg.content; }
        if (msg.role == "assistant") { assistant_count++; last_assistant = msg.content; }
    }

    if (user_count != 1 || assistant_count != 1) return;

    spdlog::info("Generating title for chat {}", event.chat_id);

    ports::LlmMessage title_msg;
    title_msg.role = "user";
    title_msg.content = "Generate a short title in the same language (max 5 words) for this chat.\n"
                        "User: " + last_user + "\n"
                        "Assistant: " + last_assistant + "\n"
                        "Title:";

    std::string accumulated;
    std::string chat_id = event.chat_id;
    std::string model = conv.model;

    m_llm.streamChat(
        chat_id,
        {title_msg},
        [this, chat_id, &accumulated](std::string_view chunk, bool is_final) {
            accumulated += std::string(chunk);
            if (is_final && !accumulated.empty()) {
                std::string title = accumulated;
                while (!title.empty() && (title.front() == '"' || title.front() == '\n' || title.front() == ' '))
                    title.erase(0, 1);
                while (!title.empty() && (title.back() == '"' || title.back() == '\n' || title.back() == ' '))
                    title.pop_back();
                if (!title.empty()) {
                    m_conv.setTitle(chat_id, title);
                }
            }
        },
        model
    );
}

void AgentEngine::onLlmError(const LlmError& event) {
    spdlog::error("LLM error in chat {}: {}", event.chat_id, event.message);
}

void AgentEngine::onToolCallCompleted(const ToolCallCompleted& /*event*/) {
    spdlog::info("Tool call completed");
}

} // namespace rook::domain
