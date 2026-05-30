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
    std::function<void(std::string_view chunk, bool is_final)> on_chunk
) {
    auto body = buildRequestBody(messages);
    auto url = m_base_url + "/v1/chat/completions";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + m_api_key},
    };

    m_sse = SseParser{};

    m_http->postStream(
        url,
        body,
        headers,
        [&on_chunk, this](std::string_view line) {
            if (!line.starts_with("data: ")) return;

            auto data = line.substr(6);
            if (data == "[DONE]") return;

            try {
                auto json = nlohmann::json::parse(data);
                if (!json.contains("choices") || json["choices"].empty()) return;

                auto& choice = json["choices"][0];
                if (!choice.contains("delta") || !choice["delta"].contains("content")) return;

                auto content = choice["delta"]["content"].get<std::string>();
                auto finish = choice.contains("finish_reason") && !choice["finish_reason"].is_null();
                on_chunk(content, finish);
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
