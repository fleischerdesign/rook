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
        std::function<void(std::string_view chunk, bool is_final, bool is_reasoning)> on_chunk,
        std::string_view model = "",
        std::function<void(std::string_view name, std::string_view arguments, std::string_view call_id)> on_tool_call = nullptr
    ) = 0;

    virtual std::vector<LlmProviderConfig> listProviders() const { return {}; }
    virtual void addProvider(const LlmProviderConfig&) {}
    virtual void updateProvider(const LlmProviderConfig&) {}
    virtual void removeProvider(std::string_view) {}
    virtual std::optional<LlmProviderConfig> activeProvider() const { return std::nullopt; }

    virtual void cancel() {}
};

struct ProviderTypeInfo {
    std::string id;
    std::string display_name;
    std::string base_url;
    std::string default_model;
    bool builtin = true;
};

class ProviderRegistry {
public:
    static ProviderRegistry& instance();

    void registerType(ProviderTypeInfo info);
    std::vector<ProviderTypeInfo> all() const;
    std::optional<ProviderTypeInfo> find(std::string_view id) const;

private:
    ProviderRegistry();
    void registerBuiltins();

    std::vector<ProviderTypeInfo> m_types;
};

} // namespace rook::ports
