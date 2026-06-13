#include "rook/adapters/client/grpc_client_llm_port.hpp"
#include "service.grpc.pb.h"
#include <spdlog/spdlog.h>

namespace rook::adapters::client {

GrpcClientLlmPort::GrpcClientLlmPort(
    std::shared_ptr<grpc::Channel> channel)
    : m_channel(std::move(channel))
{
}

void GrpcClientLlmPort::configure(const rook::ports::LlmConfig& config)
{
    m_config = config;
}

void GrpcClientLlmPort::streamChat(
    std::string_view chat_id,
    const std::vector<rook::ports::LlmMessage>& messages,
    std::function<void(std::string_view, bool, bool)> on_chunk,
    std::string_view model,
    std::function<void(std::string_view, std::string_view, std::string_view)> on_tool_call,
    std::string_view)
{
    m_cancelled.store(false, std::memory_order_release);

    rook::v1::RookService::Stub stub(m_channel);

    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now()
        + std::chrono::minutes(10));

    auto stream = stub.StreamChat(&ctx);

    rook::v1::StreamChatRequest req;
    req.set_chat_id(std::string(chat_id));
    auto* um = req.mutable_user_message();

    for (const auto& msg : messages) {
        auto* m = um->add_history();
        m->set_role(msg.role);
        m->set_content(msg.content);
        m->set_tool_call_id(msg.tool_call_id);
        m->set_tool_name(msg.tool_name);
        m->set_tool_calls(msg.tool_calls);
    }

    std::string model_str(model);
    if (!model_str.empty() && messages.empty()) {
        req.mutable_user_message()->set_model(model_str);
    } else if (!model_str.empty()) {
        req.mutable_user_message()->set_model(model_str);
    }

    if (!stream->Write(req)) {
        SPDLOG_ERROR("StreamChat: failed to write initial request");
        return;
    }

    stream->WritesDone();

    rook::v1::StreamChatResponse resp;
    while (stream->Read(&resp)) {
        if (m_cancelled.load(std::memory_order_acquire)) {
            ctx.TryCancel();
            break;
        }

        if (resp.has_chunk()) {
            auto& chunk = resp.chunk();
            on_chunk(chunk.content(), chunk.is_final(), chunk.is_reasoning());
            if (chunk.is_final())
                break;
        } else if (resp.has_tool_call()) {
            auto& tc = resp.tool_call();
            if (on_tool_call) {
                on_tool_call(tc.name(), tc.arguments(), tc.id());
            }
            break;
        } else if (resp.has_complete()) {
            break;
        } else if (resp.has_error()) {
            SPDLOG_ERROR("StreamChat server error: {} — {}",
                resp.error().code(), resp.error().message());
            on_chunk("Error: " + resp.error().message(), true, false);
            break;
        }
    }

    auto status = stream->Finish();
    if (!status.ok() && !m_cancelled.load(std::memory_order_acquire)) {
        SPDLOG_WARN("StreamChat gRPC error: {}", status.error_message());
    }
}

std::vector<rook::ports::LlmProviderConfig> GrpcClientLlmPort::listProviders() const
{
    return {{"", "Remote Server", "grpc-remote", "", "", "", true}};
}

std::optional<rook::ports::LlmProviderConfig> GrpcClientLlmPort::activeProvider() const
{
    return rook::ports::LlmProviderConfig{
        "", "Remote Server", "grpc-remote", "", "", "", true};
}

void GrpcClientLlmPort::cancel()
{
    m_cancelled.store(true, std::memory_order_release);
}

} // namespace rook::adapters::client
