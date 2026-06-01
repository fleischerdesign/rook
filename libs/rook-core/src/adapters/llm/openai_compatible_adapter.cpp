#include "rook/adapters/llm/openai_compatible_adapter.hpp"
#include "rook/adapters/llm/llm_http_client.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace rook::adapters::llm {

void SseParser::feed(std::string_view chunk, std::function<void(const SseEvent&)> on_event) {
    SseEvent event;
    for (char c : chunk) {
        if (c == '\n') {
            parseLine(m_buffer, event, on_event);
            m_buffer.clear();
        } else {
            m_buffer += c;
        }
    }
    if (!m_buffer.empty()) {
        m_buffer += '\n';
    }
}

void SseParser::parseLine(std::string_view line, SseEvent& event, std::function<void(const SseEvent&)> on_event) {
    if (line.empty()) {
        if (!event.data.empty()) {
            on_event(event);
            event = SseEvent{};
        }
        return;
    }

    if (line.starts_with("data: ")) {
        event.data = line.substr(6);
    } else if (line.starts_with("event: ")) {
        event.event = line.substr(7);
    } else if (line.starts_with("id: ")) {
        event.id = line.substr(4);
    }
}

OpenAiCompatibleAdapter::OpenAiCompatibleAdapter(
    std::unique_ptr<LlmHttpClient> http,
    std::string base_url,
    std::string default_model
)
    : m_http(std::move(http))
    , m_base_url(std::move(base_url))
    , m_model(std::move(default_model))
{}

void OpenAiCompatibleAdapter::configure(const ports::LlmConfig& config) {
    m_api_key = config.api_key;
    m_model = config.model.empty() ? m_model : config.model;
    m_system_prompt = config.system_prompt;
    m_temperature = config.temperature;
}

void OpenAiCompatibleAdapter::cancel() {
    m_http->cancel();
}

std::string OpenAiCompatibleAdapter::buildRequestBody(
    const std::vector<ports::LlmMessage>& messages,
    std::string_view tools_json
) const {
    nlohmann::json body;
    body["model"] = m_model;
    body["stream"] = true;
    body["temperature"] = m_temperature;

    body["messages"] = nlohmann::json::array();

    if (!m_system_prompt.empty()) {
        body["messages"].push_back({
            {"role", "system"},
            {"content", m_system_prompt}
        });
    }

    for (const auto& msg : messages) {
        nlohmann::json m;
        m["role"] = msg.role;

        if (!msg.content.empty()) {
            m["content"] = msg.content;
        }

        if (!msg.tool_call_id.empty()) {
            m["tool_call_id"] = msg.tool_call_id;
        }

        if (!msg.tool_calls.empty()) {
            try {
                m["tool_calls"] = nlohmann::json::parse(msg.tool_calls);
            } catch (...) {
                spdlog::warn("Failed to parse tool_calls JSON");
            }
        }

        body["messages"].push_back(std::move(m));
    }

    if (!tools_json.empty()) {
        try {
            body["tools"] = nlohmann::json::parse(std::string(tools_json));
        } catch (...) {
            spdlog::warn("Failed to parse tools JSON");
        }
    }

    return body.dump();
}

void OpenAiCompatibleAdapter::streamChat(
    std::string_view /*chat_id*/,
    const std::vector<ports::LlmMessage>& messages,
    std::function<void(std::string_view, bool, bool)> on_chunk,
    std::string_view model,
    std::function<void(std::string_view, std::string_view, std::string_view)> on_tool_call,
    std::string_view tools_json
) {
    auto body = buildRequestBody(messages, tools_json);
    spdlog::info("SSE request body (first 500): {}", body.substr(0, 500));
    if (!model.empty()) {
        auto j = nlohmann::json::parse(body);
        j["model"] = std::string(model);
        body = j.dump();
    }
    auto url = m_base_url + "/v1/chat/completions";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + m_api_key},
    };

    m_sse = SseParser{};

    using TCall = std::function<void(std::string_view, std::string_view, std::string_view)>;
    TCall tool_handler = std::move(on_tool_call);

    std::map<int, std::string> tc_ids;
    std::map<int, std::string> tc_names;
    std::map<int, std::string> tc_args;

    m_http->postStream(
        url,
        body,
        headers,
        [&on_chunk, &tool_handler, &tc_ids, &tc_names, &tc_args, this](std::string_view line) {
            try {
                if (!line.starts_with("data: ")) return;

                auto data = line.substr(6);
                if (data == "[DONE]") return;

                spdlog::info("SSE raw: {}", data.substr(0, 200));

                auto json = nlohmann::json::parse(data);
                if (!json.contains("choices") || json["choices"].empty()) return;

                auto& choice = json["choices"][0];
                auto finish_reason = choice.value("finish_reason", "");

                if (choice.contains("delta")) {
                    auto& delta = choice["delta"];

                    if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
                        on_chunk(delta["reasoning_content"].get<std::string>(), false, true);
                    }

                    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                        for (auto& tc : delta["tool_calls"]) {
                            int idx = tc.value("index", 0);
                            if (tc.contains("id") && tc["id"].is_string())
                                tc_ids[idx] = tc["id"].get<std::string>();
                            if (tc.contains("function")) {
                                auto& fn = tc["function"];
                                if (fn.contains("name") && fn["name"].is_string())
                                    tc_names[idx] = fn["name"].get<std::string>();
                                if (fn.contains("arguments") && fn["arguments"].is_string())
                                    tc_args[idx] += fn["arguments"].get<std::string>();
                            }
                        }
                    }

                    if (delta.contains("content") && delta["content"].is_string()) {
                        auto content = delta["content"].get<std::string>();
                        auto finish = !finish_reason.empty();
                        on_chunk(content, finish, false);
                    }
                }

                if (finish_reason == "tool_calls" && tool_handler) {
                    for (auto& [idx, name] : tc_names) {
                        tool_handler(name, tc_args[idx], tc_ids[idx]);
                    }
                    tc_ids.clear();
                    tc_names.clear();
                    tc_args.clear();
                }
            } catch (const std::exception& e) {
                spdlog::error("SSE error: {} | line={}", e.what(), line);
            }
        },
        [&on_chunk](int32_t status) {
            spdlog::error("LLM HTTP error: {}", status);
        }
    );
}

} // namespace rook::adapters::llm
