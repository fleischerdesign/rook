#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include <grpcpp/channel.h>
#include "rook/ports/llm_port.hpp"

namespace rook::adapters::client {

class GrpcClientLlmPort : public rook::ports::LlmPort {
public:
    explicit GrpcClientLlmPort(std::shared_ptr<grpc::Channel> channel);

    void configure(const rook::ports::LlmConfig& config) override;

    void streamChat(
        std::string_view chat_id,
        const std::vector<rook::ports::LlmMessage>& messages,
        std::function<void(std::string_view chunk,
                           bool is_final,
                           bool is_reasoning)> on_chunk,
        std::string_view model = "",
        std::function<void(std::string_view name,
                           std::string_view arguments,
                           std::string_view call_id)> on_tool_call = nullptr,
        std::string_view tools_json = ""
    ) override;

    std::vector<rook::ports::LlmProviderConfig> listProviders() const override;
    std::optional<rook::ports::LlmProviderConfig> activeProvider() const override;

    void cancel() override;

private:
    std::shared_ptr<grpc::Channel> m_channel;
    rook::ports::LlmConfig m_config;
    std::mutex m_cancel_mutex;
    std::atomic<bool> m_cancelled{false};
};

} // namespace rook::adapters::client
