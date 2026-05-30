#include "rook/adapters/llm/llm_http_client.hpp"
#include "rook/ports/llm_port.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace rook::adapters::llm {

class AnthropicAdapter final : public ports::LlmPort {
public:
    explicit AnthropicAdapter(std::unique_ptr<LlmHttpClient> http)
        : m_http(std::move(http))
    {}

    void configure(const ports::LlmConfig& config) override {
        m_api_key = config.api_key;
        m_model = config.model.empty() ? "claude-sonnet-4-20250514" : config.model;
        m_system_prompt = config.system_prompt;
        m_max_tokens = config.max_tokens;
    }

    void streamChat(
        std::string_view /*chat_id*/,
        const std::vector<ports::LlmMessage>& messages,
        std::function<void(std::string_view chunk, bool is_final)> on_chunk
    ) override {
        nlohmann::json body;
        body["model"] = m_model;
        body["max_tokens"] = m_max_tokens;
        body["stream"] = true;

        if (!m_system_prompt.empty()) {
            body["system"] = m_system_prompt;
        }

        body["messages"] = nlohmann::json::array();
        for (const auto& msg : messages) {
            body["messages"].push_back({
                {"role", msg.role},
                {"content", msg.content}
            });
        }

        std::vector<std::pair<std::string, std::string>> headers = {
            {"Content-Type", "application/json"},
            {"x-api-key", m_api_key},
            {"anthropic-version", "2023-06-01"},
        };

        m_http->postStream(
            m_base_url + "/v1/messages",
            body.dump(),
            headers,
            [&on_chunk](std::string_view line) {
                if (!line.starts_with("data: ")) return;
                auto data = line.substr(6);

                try {
                    auto json = nlohmann::json::parse(data);

                    if (json.contains("type") && json["type"] == "content_block_delta") {
                        if (json.contains("delta") && json["delta"].contains("text")) {
                            on_chunk(json["delta"]["text"].get<std::string>(), false);
                        }
                    }

                    if (json.contains("type") && json["type"] == "message_stop") {
                        on_chunk("", true);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Anthropic SSE parse error: {}", e.what());
                }
            },
            [](int32_t status) {
                spdlog::error("Anthropic HTTP error: {}", status);
            }
        );
    }

private:
    std::unique_ptr<LlmHttpClient> m_http;
    std::string m_base_url = "https://api.anthropic.com";
    std::string m_api_key;
    std::string m_model = "claude-sonnet-4-20250514";
    std::string m_system_prompt;
    int32_t m_max_tokens = 4096;
};

std::unique_ptr<ports::LlmPort> makeAnthropicAdapter() {
    return std::make_unique<AnthropicAdapter>(makeCurlHttpClient());
}

} // namespace rook::adapters::llm
