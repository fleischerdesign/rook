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
        std::function<void(std::string_view chunk, bool is_final)> on_chunk,
        std::string_view model = ""
    ) override;

    std::vector<ports::LlmProviderConfig> listProviders() const override;
    void addProvider(const ports::LlmProviderConfig& provider) override;
    void updateProvider(const ports::LlmProviderConfig& provider) override;
    void removeProvider(std::string_view id) override;
    void setDefaultProvider(std::string_view id) override;
    std::optional<ports::LlmProviderConfig> activeProvider() const override;

private:
    std::unique_ptr<ports::LlmPort> createConcreteAdapter(
        const ports::LlmProviderConfig& provider);

    std::vector<ports::LlmProviderConfig> m_providers;
    std::unique_ptr<ports::LlmPort> m_active_adapter;
    std::string m_active_provider_id;
    std::unordered_map<std::string, std::unique_ptr<ports::LlmPort>> m_provider_adapters;
};

} // namespace rook::adapters::llm
