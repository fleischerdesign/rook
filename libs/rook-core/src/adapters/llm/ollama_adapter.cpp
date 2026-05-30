#include "rook/adapters/llm/llm_http_client.hpp"
#include "rook/ports/llm_port.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace rook::adapters::llm {

class OllamaAdapter final : public ports::LlmPort {
public:
    explicit OllamaAdapter(std::unique_ptr<LlmHttpClient> http)
        : m_http(std::move(http))
    {}

    void configure(const ports::LlmConfig& config) override {
        m_model = config.model.empty() ? "llama3.1" : config.model;
        m_system_prompt = config.system_prompt;
        m_temperature = config.temperature;
    }

    void streamChat(
        std::string_view /*chat_id*/,
        const std::vector<ports::LlmMessage>& messages,
        std::function<void(std::string_view chunk, bool is_final)> on_chunk
    ) override {
        nlohmann::json body;
        body["model"] = m_model;
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
        };

        m_http->postStream(
            m_base_url + "/api/chat",
            body.dump(),
            headers,
            [&on_chunk](std::string_view line) {
                try {
                    auto json = nlohmann::json::parse(line);
                    if (json.contains("message") && json["message"].contains("content")) {
                        auto content = json["message"]["content"].get<std::string>();
                        bool done = json.value("done", false);
                        on_chunk(content, done);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Ollama SSE parse error: {}", e.what());
                }
            },
            [&on_chunk](int32_t status) {
                spdlog::error("Ollama HTTP error: {}", status);
            }
        );
    }

private:
    std::unique_ptr<LlmHttpClient> m_http;
    std::string m_base_url = "http://localhost:11434";
    std::string m_model = "llama3.1";
    std::string m_system_prompt;
    float m_temperature = 0.7f;
};

std::unique_ptr<ports::LlmPort> makeOllamaAdapter() {
    return std::make_unique<OllamaAdapter>(makeCurlHttpClient());
}

} // namespace rook::adapters::llm
