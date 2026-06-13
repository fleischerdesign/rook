#include "rook/adapters/client/grpc_client_store_port.hpp"
#include "service.grpc.pb.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace rook::adapters::client {

GrpcClientStorePort::GrpcClientStorePort(
    std::shared_ptr<grpc::Channel> channel)
    : m_channel(std::move(channel))
{
}

void GrpcClientStorePort::saveChat(const rook::ports::ChatRecord&)
{
    SPDLOG_WARN("GrpcClientStorePort: saveChat is local-only, use CRDT sync");
}

std::optional<rook::ports::ChatRecord> GrpcClientStorePort::loadChat(
    std::string_view)
{
    SPDLOG_WARN("GrpcClientStorePort: loadChat is local-only, use CRDT sync");
    return std::nullopt;
}

std::vector<rook::ports::ChatRecord> GrpcClientStorePort::listChats()
{
    return {};
}

void GrpcClientStorePort::deleteChat(std::string_view)
{
}

std::string GrpcClientStorePort::loadConfig()
{
    rook::v1::RookService::Stub stub(m_channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now()
        + std::chrono::seconds(5));

    rook::v1::GetConfigRequest req;
    rook::v1::GetConfigResponse resp;

    auto status = stub.GetConfig(&ctx, req, &resp);
    if (!status.ok()) {
        SPDLOG_WARN("GetConfig failed: {}", status.error_message());
        return "{}";
    }

    nlohmann::json j;
    for (const auto& entry : resp.entries()) {
        try {
            j[entry.key()] = nlohmann::json::parse(entry.value());
        } catch (...) {
            j[entry.key()] = entry.value();
        }
    }

    return j.dump();
}

void GrpcClientStorePort::saveConfig(std::string_view json)
{
    rook::v1::RookService::Stub stub(m_channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now()
        + std::chrono::seconds(5));

    rook::v1::UpdateConfigRequest req;
    rook::v1::UpdateConfigResponse resp;

    try {
        auto j = nlohmann::json::parse(json);
        if (j.is_object()) {
            for (auto& [key, value] : j.items()) {
                auto* entry = req.add_entries();
                entry->set_key(key);
                entry->set_value(value.is_string()
                    ? value.get<std::string>() : value.dump());
            }
        }
    } catch (...) {
        SPDLOG_ERROR("saveConfig: invalid JSON");
        return;
    }

    auto status = stub.UpdateConfig(&ctx, req, &resp);
    if (!status.ok()) {
        SPDLOG_WARN("UpdateConfig failed: {}", status.error_message());
    }
}

} // namespace rook::adapters::client
