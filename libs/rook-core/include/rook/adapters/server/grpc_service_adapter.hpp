#pragma once

#include <memory>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <functional>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"
#include "rook/core/domain_actor.hpp"

namespace rook::ports {
class StorePort;
class ExtensionPort;
}

namespace rook::sync {
class SyncEngine;
}

namespace rook::adapters::server {

class GrpcServiceAdapter final : public rook::v1::RookService::Service {
public:
    GrpcServiceAdapter(
        rook::core::DomainActor& actor,
        rook::sync::SyncEngine& sync,
        rook::ports::StorePort& store,
        rook::ports::ExtensionPort* extensions
    );

    void onDomainEvent(rook::domain::DomainEvent event);
    void shutdown();

    grpc::Status SendMessage(
        grpc::ServerContext* context,
        const rook::v1::SendMessageRequest* request,
        rook::v1::SendMessageResponse* response
    ) override;

    grpc::Status StreamChat(
        grpc::ServerContext* context,
        grpc::ServerReaderWriter<
            rook::v1::StreamChatResponse,
            rook::v1::StreamChatRequest
        >* stream
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

private:
    void processStreamRequest(const rook::v1::StreamChatRequest& req);
    void emitStreamEvent(const std::string& chat_id, rook::v1::StreamChatResponse resp);

    struct StreamState {
        std::mutex mtx;
        std::condition_variable cv;
        std::deque<rook::v1::StreamChatResponse> pending;
        bool closed = false;
    };

    rook::core::DomainActor& m_actor;
    rook::sync::SyncEngine& m_sync;
    rook::ports::StorePort& m_store;
    rook::ports::ExtensionPort* m_extensions;

    std::mutex m_mutex;
    std::atomic<bool> m_shutdown{false};
    std::unordered_map<std::string, std::shared_ptr<StreamState>> m_streams;
};

} // namespace rook::adapters::server
