#pragma once

#include <memory>
#include <string>
#include "service.grpc.pb.h"

namespace rook::adapters::server {

class TenantManager;
class GrpcServiceAdapter;

class TenantServiceAdapter final : public rook::v1::RookService::Service {
public:
    TenantServiceAdapter(TenantManager& tenants, std::string jwt_secret);

    grpc::Status SendMessage(
        grpc::ServerContext* context,
        const rook::v1::SendMessageRequest* request,
        rook::v1::SendMessageResponse* response
    ) override;

    grpc::Status StreamChat(
        grpc::ServerContext* context,
        grpc::ServerReaderWriter<
            rook::v1::StreamChatResponse,
            rook::v1::StreamChatRequest>* stream
    ) override;

    grpc::Status ListExtensions(
        grpc::ServerContext* context,
        const rook::v1::ListExtensionsRequest* request,
        rook::v1::ListExtensionsResponse* response
    ) override;

    grpc::Status InstallExtension(
        grpc::ServerContext* context,
        const rook::v1::InstallExtensionRequest* request,
        rook::v1::InstallExtensionResponse* response
    ) override;

    grpc::Status RemoveExtension(
        grpc::ServerContext* context,
        const rook::v1::RemoveExtensionRequest* request,
        rook::v1::RemoveExtensionResponse* response
    ) override;

    grpc::Status GetConfig(
        grpc::ServerContext* context,
        const rook::v1::GetConfigRequest* request,
        rook::v1::GetConfigResponse* response
    ) override;

    grpc::Status UpdateConfig(
        grpc::ServerContext* context,
        const rook::v1::UpdateConfigRequest* request,
        rook::v1::UpdateConfigResponse* response
    ) override;

    grpc::Status ListClients(
        grpc::ServerContext* context,
        const rook::v1::ListClientsRequest* request,
        rook::v1::ListClientsResponse* response
    ) override;

    grpc::Status DelegateTask(
        grpc::ServerContext* context,
        const rook::v1::DelegateTaskRequest* request,
        rook::v1::DelegateTaskResponse* response
    ) override;

    grpc::Status PushSyncState(
        grpc::ServerContext* context,
        const rook::v1::PushSyncStateRequest* request,
        rook::v1::PushSyncStateResponse* response
    ) override;

    grpc::Status PullSyncState(
        grpc::ServerContext* context,
        const rook::v1::PullSyncStateRequest* request,
        rook::v1::PullSyncStateResponse* response
    ) override;

    grpc::Status CreateTenant(
        grpc::ServerContext* context,
        const rook::v1::CreateTenantRequest* request,
        rook::v1::CreateTenantResponse* response
    ) override;

    grpc::Status ListTenants(
        grpc::ServerContext* context,
        const rook::v1::ListTenantsRequest* request,
        rook::v1::ListTenantsResponse* response
    ) override;

    grpc::Status DeleteTenant(
        grpc::ServerContext* context,
        const rook::v1::DeleteTenantRequest* request,
        rook::v1::DeleteTenantResponse* response
    ) override;

private:
    GrpcServiceAdapter* ensureAdapter(const std::string& tenant_id);
    GrpcServiceAdapter* ensureAdapterForContext(
        grpc::ServerContext* context);

    TenantManager& m_tenants;
    std::string m_jwt_secret;
};

} // namespace rook::adapters::server
