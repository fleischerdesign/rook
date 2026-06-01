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
    const std::vector<ports::LlmMessage>& messages
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
        body["messages"].push_back({
            {"role", msg.role},
            {"content", msg.content}
        });
    }

    return body.dump();
}

void OpenAiCompatibleAdapter::streamChat(
    std::string_view /*chat_id*/,
    const std::vector<ports::LlmMessage>& messages,
    std::function<void(std::string_view, bool, bool)> on_chunk,
    std::string_view model,
    std::function<void(std::string_view, std::string_view, std::string_view)> on_tool_call
) {
    auto body = buildRequestBody(messages);
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

    m_http->postStream(
        url,
        body,
        headers,
        [&on_chunk, &tool_handler, this](std::string_view line) {
            if (!line.starts_with("data: ")) return;

            auto data = line.substr(6);
            if (data == "[DONE]") return;

            try {
                auto json = nlohmann::json::parse(data);
                if (!json.contains("choices") || json["choices"].empty()) return;

                auto& choice = json["choices"][0];
                if (!choice.contains("delta")) return;

                auto& delta = choice["delta"];

                if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null()) {
                    on_chunk(delta["reasoning_content"].get<std::string>(), false, true);
                }

                if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                    for (auto& tc : delta["tool_calls"]) {
                        std::string call_id = tc.value("id", "");
                        std::string fn_name;
                        std::string fn_args;
                        if (tc.contains("function")) {
                            fn_name = tc["function"].value("name", "");
                            fn_args = tc["function"].value("arguments", "");
                        }
                        if (tool_handler && !fn_name.empty()) {
                            tool_handler(fn_name, fn_args, call_id);
                        }
                    }
                }

                if (delta.contains("content") && !delta["content"].is_null()) {
                    auto content = delta["content"].get<std::string>();
                    auto finish = choice.contains("finish_reason") && !choice["finish_reason"].is_null();
                    on_chunk(content, finish, false);
                }
            } catch (const std::exception& e) {
                spdlog::error("SSE parse error: {}", e.what());
            }
        },
        [&on_chunk](int32_t status) {
            spdlog::error("LLM HTTP error: {}", status);
        }
    );
}

} // namespace rook::adapters::llm
