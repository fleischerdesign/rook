#include "rook/adapters/client/grpc_client_extension_port.hpp"
#include "service.grpc.pb.h"
#include <spdlog/spdlog.h>

namespace rook::adapters::client {

GrpcClientExtensionPort::GrpcClientExtensionPort(
    std::shared_ptr<grpc::Channel> channel)
    : m_channel(std::move(channel))
{
}

std::vector<rook::adapters::extension::InstalledExtension>
GrpcClientExtensionPort::listInstalled()
{
    rook::v1::RookService::Stub stub(m_channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now()
        + std::chrono::seconds(5));

    rook::v1::ListExtensionsRequest req;
    rook::v1::ListExtensionsResponse resp;

    auto status = stub.ListExtensions(&ctx, req, &resp);
    if (!status.ok())
        return {};

    std::vector<rook::adapters::extension::InstalledExtension> result;
    for (const auto& ext : resp.extensions()) {
        rook::adapters::extension::InstalledExtension ie;
        ie.name = ext.id();
        ie.display_name = ext.name();
        ie.version = ext.version();
        ie.description = ext.description();
        result.push_back(std::move(ie));
    }
    return result;
}

bool GrpcClientExtensionPort::install(std::string_view github_url)
{
    rook::v1::RookService::Stub stub(m_channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now()
        + std::chrono::seconds(30));

    rook::v1::InstallExtensionRequest req;
    req.set_id(std::string(github_url));
    rook::v1::InstallExtensionResponse resp;

    auto status = stub.InstallExtension(&ctx, req, &resp);
    return status.ok();
}

bool GrpcClientExtensionPort::uninstall(std::string_view name)
{
    rook::v1::RookService::Stub stub(m_channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now()
        + std::chrono::seconds(10));

    rook::v1::RemoveExtensionRequest req;
    req.set_id(std::string(name));
    rook::v1::RemoveExtensionResponse resp;

    auto status = stub.RemoveExtension(&ctx, req, &resp);
    return status.ok();
}

bool GrpcClientExtensionPort::update(std::string_view name)
{
    return install(name);
}

} // namespace rook::adapters::client
