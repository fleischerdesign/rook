#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <chrono>
#include <optional>
#include <memory>

namespace rook::ports {

struct LlmProviderConfig {
    std::string id;
    std::string display_name;
    std::string type;          // "ollama" | "openai" | "deepseek" | "anthropic"
    std::string base_url;
    std::string api_key;       // empty = read from libsecret
    std::string default_model;
    bool enabled = true;
    bool is_default = false;
};

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

    virtual std::vector<LlmProviderConfig> listProviders() const { return {}; }
    virtual void addProvider(const LlmProviderConfig&) {}
    virtual void updateProvider(const LlmProviderConfig&) {}
    virtual void removeProvider(std::string_view) {}
    virtual void setDefaultProvider(std::string_view) {}
    virtual std::optional<LlmProviderConfig> activeProvider() const { return std::nullopt; }
};

struct ProviderDefaults {
    static std::string displayName(std::string_view type);
    static std::string baseUrl(std::string_view type);
    static std::string defaultModel(std::string_view type);
};

} // namespace rook::ports
