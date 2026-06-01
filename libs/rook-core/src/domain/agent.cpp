#include "rook/domain/agent.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace rook::domain {

AgentEngine::AgentEngine(EventBus& bus, ports::LlmPort& llm,
                         ConversationManager& conv, ports::ToolPort& tool)
    : m_bus(bus)
    , m_llm(llm)
    , m_conv(conv)
    , m_tool(tool)
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
    m_conv.setModel(event.chat_id, event.model);
    m_conv.addMessage(event.chat_id, std::move(msg));

    m_pending_tool_calls.clear();
    runLlm(event.chat_id, event.model);
}

static std::string buildToolsJson(const std::vector<ports::ToolDefinition>& tools) {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& t : tools) {
        nlohmann::json tool;
        tool["type"] = "function";
        tool["function"]["name"] = t.name;
        tool["function"]["description"] = t.description;

        nlohmann::json props = nlohmann::json::object();
        nlohmann::json required = nlohmann::json::array();
        for (auto& p : t.parameters) {
            nlohmann::json prop;
            prop["type"] = p.type;
            prop["description"] = p.description;
            props[p.name] = prop;
            if (p.required) required.push_back(p.name);
        }
        tool["function"]["parameters"]["type"] = "object";
        tool["function"]["parameters"]["properties"] = props;
        if (!required.empty()) {
            tool["function"]["parameters"]["required"] = required;
        }

        arr.push_back(std::move(tool));
    }
    return arr.dump();
}

void AgentEngine::runLlm(std::string chat_id, std::string model) {
    auto messages = m_conv.buildLlmMessages(chat_id);

    auto tools = m_tool.listTools();
    auto tools_json = buildToolsJson(tools);

    m_bus.publish(LlmRequested{chat_id, ""});

    m_llm.streamChat(
        chat_id,
        messages,
        [this, chat_id](std::string_view chunk, bool is_final, bool is_reasoning) {
            if (!chunk.empty()) {
                m_bus.publish(LlmStreamChunk{
                    .chat_id = chat_id,
                    .content = std::string(chunk),
                    .is_final = is_final,
                    .is_reasoning = is_reasoning,
                });
            }

            if (is_final && !is_reasoning) {
                m_bus.publish(LlmCompleted{chat_id, 0});
            }
        },
        model,
        [this, chat_id](std::string_view name, std::string_view arguments,
                         std::string_view call_id) {
            ports::ToolCall call;
            call.id = std::string(call_id);
            call.name = std::string(name);
            call.arguments = std::string(arguments);

            m_bus.publish(ToolCallRequested{
                .chat_id = chat_id,
                .tool_name = call.name,
                .arguments = call.arguments,
                .call_id = call.id,
            });

            m_pending_tool_calls.push_back(std::move(call));
        },
        tools_json
    );
}

void AgentEngine::onLlmChunk(const LlmStreamChunk& chunk) {
    if (chunk.chat_id.empty() || chunk.chat_id.starts_with("__title__")) return;

    if (chunk.is_reasoning) {
        m_conv.updateReasoningChunk(chunk.chat_id, chunk.content);
    } else {
        m_conv.updateAssistantChunk(chunk.chat_id, chunk.content);
    }
}

void AgentEngine::onLlmCompleted(const LlmCompleted& event) {
    spdlog::info("LLM response complete for chat {}", event.chat_id);

    if (processPendingToolCalls(event.chat_id)) return;

    m_conv.saveActiveConversation();

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

    ports::LlmMessage sys_msg;
    sys_msg.role = "system";
    sys_msg.content = "You are a title generator. Reply with ONLY the raw title text, "
                      "no quotes, no prefixes, no markdown, no explanation.";

    ports::LlmMessage title_msg;
    title_msg.role = "user";
    title_msg.content = "Generate a short title (max 5 words) in the same language "
                        "for this conversation:\n\n"
                        "User: " + last_user + "\n"
                        "Assistant: " + last_assistant;

    std::string accumulated;
    std::string chat_id = event.chat_id;
    std::string model = conv.model;

    auto cleanTitle = [](std::string s) -> std::string {
        s.erase(0, s.find_first_not_of(" \t\n\r\"'"));
        s.erase(s.find_last_not_of(" \t\n\r\"'") + 1);
        for (auto prefix : {"Title:", "title:", "Title :", "title :",
                            "**", "##", "#"}) {
            if (s.starts_with(prefix)) { s.erase(0, std::strlen(prefix)); break; }
        }
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    };

    m_llm.streamChat(
        chat_id,
        {sys_msg, title_msg},
        [this, chat_id, &accumulated, &cleanTitle](std::string_view chunk, bool is_final, bool is_reasoning) {
            if (is_reasoning) return;
            accumulated += std::string(chunk);
            if (is_final && !accumulated.empty()) {
                std::string title = cleanTitle(accumulated);
                if (!title.empty()) {
                    m_conv.setTitle(chat_id, std::move(title));
                }
            }
        },
        model
    );
}

bool AgentEngine::processPendingToolCalls(std::string_view chat_id) {
    if (m_pending_tool_calls.empty()) return false;

    spdlog::info("Processing {} tool calls for chat {}", m_pending_tool_calls.size(), chat_id);

    auto conv = m_conv.open(chat_id);
    std::string model = conv.model;

    nlohmann::json tc_json = nlohmann::json::array();
    for (auto& tc : m_pending_tool_calls) {
        nlohmann::json item;
        item["id"] = tc.id;
        item["type"] = "function";
        item["function"]["name"] = tc.name;
        item["function"]["arguments"] = tc.arguments;
        tc_json.push_back(std::move(item));
    }

    m_conv.setAssistantToolCalls(chat_id, tc_json.dump());

    auto calls = std::move(m_pending_tool_calls);
    m_pending_tool_calls.clear();

    for (auto& call : calls) {
        auto result = m_tool.execute(call);

        m_bus.publish(ToolCallCompleted{
            .chat_id = std::string(chat_id),
            .call_id = call.id,
            .result = result.content,
            .is_error = result.is_error,
        });

        ChatMessage tool_msg;
        tool_msg.role = "tool";
        tool_msg.content = result.content;
        tool_msg.tool_call_id = call.id;
        m_conv.addMessage(chat_id, std::move(tool_msg));

        spdlog::info("Tool {} completed: {}", call.name,
                     result.is_error ? "error" : "success");
    }

    runLlm(std::string(chat_id), std::move(model));
    return true;
}

void AgentEngine::onLlmError(const LlmError& event) {
    spdlog::error("LLM error in chat {}: {}", event.chat_id, event.message);
}

void AgentEngine::onToolCallCompleted(const ToolCallCompleted& /*event*/) {
    spdlog::info("Tool call completed");
}

} // namespace rook::domain
