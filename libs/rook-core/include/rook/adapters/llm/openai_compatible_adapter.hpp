#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <memory>
#include "rook/ports/llm_port.hpp"

namespace rook::adapters::llm {

class LlmHttpClient;

struct SseEvent {
    std::string id;
    std::string event;
    std::string data;
};

class SseParser {
public:
    void feed(std::string_view chunk, std::function<void(const SseEvent&)> on_event);

private:
    std::string m_buffer;
    void parseLine(std::string_view line, SseEvent& event, std::function<void(const SseEvent&)> on_event);
};

class OpenAiCompatibleAdapter : public ports::LlmPort {
public:
    explicit OpenAiCompatibleAdapter(
        std::unique_ptr<LlmHttpClient> http,
        std::string base_url,
        std::string default_model
    );

    void configure(const ports::LlmConfig& config) override;

    void cancel() override;

    void streamChat(
        std::string_view chat_id,
        const std::vector<ports::LlmMessage>& messages,
        std::function<void(std::string_view chunk, bool is_final, bool is_reasoning)> on_chunk,
        std::string_view model = "",
        std::function<void(std::string_view name, std::string_view arguments, std::string_view call_id)> on_tool_call = nullptr
    ) override;

protected:
    std::unique_ptr<LlmHttpClient> m_http;
    std::string m_base_url;
    std::string m_api_key;
    std::string m_model;
    std::string m_system_prompt;
    float m_temperature = 0.7f;

    std::string buildRequestBody(const std::vector<ports::LlmMessage>& messages) const;

private:
    SseParser m_sse;
};

} // namespace rook::adapters::llm
