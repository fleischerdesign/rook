#pragma once

#include <memory>
#include <functional>
#include <thread>
#include <stop_token>
#include "rook/ports/llm_port.hpp"

namespace rook::adapters::llm {

class ThreadedLlmAdapter : public ports::LlmPort {
public:
    explicit ThreadedLlmAdapter(std::unique_ptr<ports::LlmPort> inner);
    ~ThreadedLlmAdapter() override;

    void configure(const ports::LlmConfig& config) override;

    void streamChat(
        std::string_view chat_id,
        const std::vector<ports::LlmMessage>& messages,
        std::function<void(std::string_view chunk, bool is_final, bool is_reasoning)> on_chunk,
        std::string_view model = ""
    ) override;

    std::vector<ports::LlmProviderConfig> listProviders() const override;
    void addProvider(const ports::LlmProviderConfig& provider) override;
    void updateProvider(const ports::LlmProviderConfig& provider) override;
    void removeProvider(std::string_view id) override;
    std::optional<ports::LlmProviderConfig> activeProvider() const override;

    void cancel();

private:
    std::unique_ptr<ports::LlmPort> m_inner;
    std::jthread m_thread;
    std::stop_source m_stop_source;
};

} // namespace rook::adapters::llm
