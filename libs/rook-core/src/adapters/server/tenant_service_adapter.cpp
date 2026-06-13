#include "rook/adapters/server/tenant_service_adapter.hpp"
#include "rook/adapters/server/tenant_manager.hpp"
#include "rook/adapters/server/grpc_service_adapter.hpp"
#include "rook/auth/jwt.hpp"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <mutex>

namespace rook::adapters::server {

static std::string extractTenantId(grpc::ServerContext* context,
                                    const std::string& jwt_secret)
{
    if (jwt_secret.empty())
        return "default";

    auto auth = context->client_metadata().find("authorization");
    if (auth == context->client_metadata().end())
        return "";

    std::string value(auth->second.data(), auth->second.size());
    const std::string prefix = "Bearer ";
    if (value.size() <= prefix.size()
        || value.substr(0, prefix.size()) != prefix)
        return "";

    auto token = value.substr(prefix.size());
    auto payload = rook::auth::verifyToken(token, jwt_secret);
    if (!payload)
        return "";

    return payload->tenant_id;
}

TenantServiceAdapter::TenantServiceAdapter(
    TenantManager& tenants, std::string jwt_secret)
    : m_tenants(tenants)
    , m_jwt_secret(std::move(jwt_secret))
{
}

GrpcServiceAdapter* TenantServiceAdapter::ensureAdapterForContext(
    grpc::ServerContext* context)
{
    auto tenant_id = extractTenantId(context, m_jwt_secret);
    if (tenant_id.empty()) {
        return nullptr;
    }
    return ensureAdapter(tenant_id);
}

GrpcServiceAdapter* TenantServiceAdapter::ensureAdapter(
    const std::string& tenant_id)
{
    auto* tenant = m_tenants.getOrCreate(tenant_id);
    if (!tenant) return nullptr;
    return tenant->adapter.get();
}

grpc::Status TenantServiceAdapter::SendMessage(
    grpc::ServerContext* context,
    const rook::v1::SendMessageRequest* request,
    rook::v1::SendMessageResponse* response)
{
    auto* adapter = ensureAdapterForContext(context);
    if (!adapter) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
            "invalid or missing authorization token");
    }
    return adapter->SendMessage(context, request, response);
}

grpc::Status TenantServiceAdapter::StreamChat(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<
        rook::v1::StreamChatResponse,
        rook::v1::StreamChatRequest>* stream)
{
    auto* adapter = ensureAdapterForContext(context);
    if (!adapter) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
            "invalid or missing authorization token");
    }
    return adapter->StreamChat(context, stream);
}

grpc::Status TenantServiceAdapter::ListExtensions(
    grpc::ServerContext* context,
    const rook::v1::ListExtensionsRequest* request,
    rook::v1::ListExtensionsResponse* response)
{
    auto* adapter = ensureAdapterForContext(context);
    if (!adapter) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
            "invalid or missing authorization token");
    }
    return adapter->ListExtensions(context, request, response);
}

grpc::Status TenantServiceAdapter::InstallExtension(
    grpc::ServerContext* context,
    const rook::v1::InstallExtensionRequest* request,
    rook::v1::InstallExtensionResponse* response)
{
    auto* adapter = ensureAdapterForContext(context);
    if (!adapter) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
            "invalid or missing authorization token");
    }
    return adapter->InstallExtension(context, request, response);
}

grpc::Status TenantServiceAdapter::RemoveExtension(
    grpc::ServerContext* context,
    const rook::v1::RemoveExtensionRequest* request,
    rook::v1::RemoveExtensionResponse* response)
{
    auto* adapter = ensureAdapterForContext(context);
    if (!adapter) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
            "invalid or missing authorization token");
    }
    return adapter->RemoveExtension(context, request, response);
}

grpc::Status TenantServiceAdapter::GetConfig(
    grpc::ServerContext* context,
    const rook::v1::GetConfigRequest* request,
    rook::v1::GetConfigResponse* response)
{
    auto* adapter = ensureAdapterForContext(context);
    if (!adapter) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
            "invalid or missing authorization token");
    }
    return adapter->GetConfig(context, request, response);
}

grpc::Status TenantServiceAdapter::UpdateConfig(
    grpc::ServerContext* context,
    const rook::v1::UpdateConfigRequest* request,
    rook::v1::UpdateConfigResponse* response)
{
    auto* adapter = ensureAdapterForContext(context);
    if (!adapter) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
            "invalid or missing authorization token");
    }
    return adapter->UpdateConfig(context, request, response);
}

grpc::Status TenantServiceAdapter::ListClients(
    grpc::ServerContext* context,
    const rook::v1::ListClientsRequest* request,
    rook::v1::ListClientsResponse* response)
{
    auto* adapter = ensureAdapterForContext(context);
    if (!adapter) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
            "invalid or missing authorization token");
    }
    return adapter->ListClients(context, request, response);
}

grpc::Status TenantServiceAdapter::DelegateTask(
    grpc::ServerContext* context,
    const rook::v1::DelegateTaskRequest* request,
    rook::v1::DelegateTaskResponse* response)
{
    auto* adapter = ensureAdapterForContext(context);
    if (!adapter) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
            "invalid or missing authorization token");
    }
    return adapter->DelegateTask(context, request, response);
}

grpc::Status TenantServiceAdapter::PushSyncState(
    grpc::ServerContext* context,
    const rook::v1::PushSyncStateRequest* request,
    rook::v1::PushSyncStateResponse* response)
{
    auto* adapter = ensureAdapterForContext(context);
    if (!adapter) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
            "invalid or missing authorization token");
    }
    return adapter->PushSyncState(context, request, response);
}

grpc::Status TenantServiceAdapter::PullSyncState(
    grpc::ServerContext* context,
    const rook::v1::PullSyncStateRequest* request,
    rook::v1::PullSyncStateResponse* response)
{
    auto* adapter = ensureAdapterForContext(context);
    if (!adapter) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
            "invalid or missing authorization token");
    }
    return adapter->PullSyncState(context, request, response);
}

grpc::Status TenantServiceAdapter::CreateTenant(
    grpc::ServerContext*,
    const rook::v1::CreateTenantRequest* request,
    rook::v1::CreateTenantResponse* response)
{
    if (m_jwt_secret.empty()) {
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
            "multi-tenancy not enabled (set --jwt-secret)");
    }

    auto payload = rook::auth::JwtPayload{
        request->tenant_id(),
        "rookd",
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()
            + 86400 * 365, // 1 year
    };

    response->set_token(
        rook::auth::generateToken(payload, m_jwt_secret));

    m_tenants.getOrCreate(request->tenant_id());
    SPDLOG_INFO("Created tenant: {}", request->tenant_id());
    return grpc::Status::OK;
}

grpc::Status TenantServiceAdapter::ListTenants(
    grpc::ServerContext*,
    const rook::v1::ListTenantsRequest*,
    rook::v1::ListTenantsResponse* response)
{
    for (const auto& id : m_tenants.listTenants())
        response->add_tenant_ids(id);
    return grpc::Status::OK;
}

grpc::Status TenantServiceAdapter::DeleteTenant(
    grpc::ServerContext*,
    const rook::v1::DeleteTenantRequest* request,
    rook::v1::DeleteTenantResponse*)
{
    auto* tenant = m_tenants.get(request->tenant_id());
    if (!tenant) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND,
            "tenant not found");
    }

    if (tenant->actor)
        tenant->actor->stop();

    SPDLOG_INFO("Deleted tenant: {}", request->tenant_id());
    return grpc::Status::OK;
}

} // namespace rook::adapters::server
