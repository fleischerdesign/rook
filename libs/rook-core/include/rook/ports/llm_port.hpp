#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <chrono>
#include <optional>

namespace rook::ports {

struct LlmMessage {
    std::string role;
    std::string content;
};

struct LlmConfig {
    std::string provider;
    std::string model;
    std::string api_key;
    std::string base_url;
    int32_t max_tokens = 4096;
    float temperature = 0.7f;
    std::string system_prompt;
};

class LlmPort {
public:
    virtual ~LlmPort() = default;

    virtual void configure(const LlmConfig& config) = 0;

    virtual void streamChat(
        std::string_view chat_id,
        const std::vector<LlmMessage>& messages,
        std::function<void(std::string_view chunk, bool is_final)> on_chunk
    ) = 0;
};

} // namespace rook::ports
