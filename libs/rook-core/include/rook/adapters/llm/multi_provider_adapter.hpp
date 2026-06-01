#pragma once

#include <memory>
#include <unordered_map>
#include "rook/ports/llm_port.hpp"
#include "rook/adapters/llm/llm_http_client.hpp"

namespace rook::adapters::llm {

class MultiProviderLlmAdapter : public ports::LlmPort {
public:
    MultiProviderLlmAdapter();
    ~MultiProviderLlmAdapter() override = default;

    void configure(const ports::LlmConfig& config) override;

    void streamChat(
        std::string_view chat_id,
        const std::vector<ports::LlmMessage>& messages,
        std::function<void(std::string_view chunk, bool is_final, bool is_reasoning)> on_chunk,
        std::string_view model = "",
        std::function<void(std::string_view name, std::string_view arguments, std::string_view call_id)> on_tool_call = nullptr
    ) override;

    std::vector<ports::LlmProviderConfig> listProviders() const override;
    void addProvider(const ports::LlmProviderConfig& provider) override;
    void updateProvider(const ports::LlmProviderConfig& provider) override;
    void removeProvider(std::string_view id) override;
    std::optional<ports::LlmProviderConfig> activeProvider() const override;

private:
    std::unique_ptr<ports::LlmPort> createConcreteAdapter(
        const ports::LlmProviderConfig& provider);

    std::vector<ports::LlmProviderConfig> m_providers;
    std::unordered_map<std::string, std::unique_ptr<ports::LlmPort>> m_provider_adapters;
};

} // namespace rook::adapters::llm
