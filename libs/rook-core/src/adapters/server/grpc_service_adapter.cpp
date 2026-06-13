#include "rook/adapters/server/grpc_service_adapter.hpp"
#include "rook/core/actor_messages.hpp"
#include "rook/domain/events.hpp"
#include "rook/ports/store_port.hpp"
#include "rook/ports/extension_port.hpp"
#include "rook/sync/sync_engine.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <variant>

namespace rook::adapters::server {

static rook::v1::StreamChatResponse makeBaseResponse(const std::string& chat_id)
{
    rook::v1::StreamChatResponse resp;
    resp.set_chat_id(chat_id);
    return resp;
}

GrpcServiceAdapter::GrpcServiceAdapter(
    rook::core::DomainActor& actor,
    rook::sync::SyncEngine& sync,
    rook::ports::StorePort& store,
    rook::ports::ExtensionPort* extensions)
    : m_actor(actor)
    , m_sync(sync)
    , m_store(store)
    , m_extensions(extensions)
{
}

void GrpcServiceAdapter::onDomainEvent(rook::domain::DomainEvent event)
{
    if (m_shutdown.load(std::memory_order_acquire))
        return;

    std::visit([this](auto&& ev) {
        using T = std::decay_t<decltype(ev)>;

        if constexpr (std::is_same_v<T, rook::domain::LlmStreamChunk>) {
            auto resp = makeBaseResponse(ev.chat_id);
            auto* chunk = resp.mutable_chunk();
            chunk->set_content(ev.content);
            chunk->set_is_final(ev.is_final);
            chunk->set_is_reasoning(ev.is_reasoning);
            emitStreamEvent(ev.chat_id, std::move(resp));
        }
        else if constexpr (std::is_same_v<T, rook::domain::ToolCallRequested>) {
            auto resp = makeBaseResponse(ev.chat_id);
            auto* tc = resp.mutable_tool_call();
            tc->set_id(ev.call_id);
            tc->set_name(ev.tool_name);
            tc->set_arguments(ev.arguments);
            emitStreamEvent(ev.chat_id, std::move(resp));
        }
        else if constexpr (std::is_same_v<T, rook::domain::LlmCompleted>) {
            auto resp = makeBaseResponse(ev.chat_id);
            auto* cmp = resp.mutable_complete();
            cmp->set_tokens_used(ev.tokens_used);
            emitStreamEvent(ev.chat_id, std::move(resp));
        }
        else if constexpr (std::is_same_v<T, rook::domain::LlmError>) {
            auto resp = makeBaseResponse(ev.chat_id);
            auto* err = resp.mutable_error();
            err->set_code("llm_error");
            err->set_message(ev.message);
            emitStreamEvent(ev.chat_id, std::move(resp));
        }
    }, event);
}

void GrpcServiceAdapter::shutdown()
{
    m_shutdown.store(true, std::memory_order_release);

    std::lock_guard lock(m_mutex);
    for (auto& [id, state] : m_streams) {
        std::lock_guard sl(state->mtx);
        state->closed = true;
        state->cv.notify_all();
    }
}

void GrpcServiceAdapter::processStreamRequest(
    const rook::v1::StreamChatRequest& req)
{
    std::string chat_id = req.chat_id();

    if (req.has_user_message()) {
        auto& um = req.user_message();
        m_actor.post(rook::domain::ActorUserInput{
            chat_id,
            um.content(),
            um.model(),
        });

        SPDLOG_INFO("StreamChat user message on chat={}, model={}",
            chat_id, um.model().empty() ? "(default)" : um.model());
    }
    else if (req.has_tool_results()) {
        for (const auto& tr : req.tool_results().results()) {
            m_actor.post(rook::domain::ActorToolResult{
                chat_id,
                tr.call_id(),
                tr.content(),
                tr.is_error(),
            });
        }
        SPDLOG_INFO("StreamChat tool results on chat={}", chat_id);
    }
    else if (req.has_cancel()) {
        m_actor.post(rook::domain::ActorCancelChat{chat_id});
        SPDLOG_INFO("StreamChat cancel on chat={}", chat_id);
    }
}

void GrpcServiceAdapter::emitStreamEvent(
    const std::string& chat_id, rook::v1::StreamChatResponse resp)
{
    std::lock_guard lock(m_mutex);
    auto it = m_streams.find(chat_id);
    if (it == m_streams.end())
        return;

    auto& state = it->second;
    {
        std::lock_guard sl(state->mtx);
        state->pending.push_back(std::move(resp));
    }
    state->cv.notify_one();
}

grpc::Status GrpcServiceAdapter::SendMessage(
    grpc::ServerContext*,
    const rook::v1::SendMessageRequest* request,
    rook::v1::SendMessageResponse* response)
{
    m_actor.post(rook::domain::ActorUserInput{
        request->chat_id(),
        request->content(),
        "",
    });

    response->set_message_id(request->chat_id());
    SPDLOG_INFO("SendMessage on chat={}", request->chat_id());
    return grpc::Status::OK;
}

grpc::Status GrpcServiceAdapter::StreamChat(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<
        rook::v1::StreamChatResponse,
        rook::v1::StreamChatRequest>* stream)
{
    rook::v1::StreamChatRequest first_req;

    if (!stream->Read(&first_req)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
            "expected initial StreamChatRequest");
    }

    std::string chat_id = first_req.chat_id();
    if (chat_id.empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
            "chat_id must not be empty");
    }

    if (!first_req.has_user_message()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
            "first request must contain user_message");
    }

    auto state = std::make_shared<StreamState>();
    {
        std::lock_guard lock(m_mutex);
        m_streams[chat_id] = state;
    }

    SPDLOG_INFO("StreamChat started on chat={}", chat_id);

    processStreamRequest(first_req);

    std::atomic<bool> reader_done{false};
    std::thread reader([this, stream, &reader_done]() {
        rook::v1::StreamChatRequest req;
        while (stream->Read(&req)) {
            processStreamRequest(req);
        }
        reader_done = true;
    });

    while (!reader_done.load(std::memory_order_acquire)
           && !context->IsCancelled())
    {
        rook::v1::StreamChatResponse resp;

        {
            std::unique_lock sl(state->mtx);
            state->cv.wait_for(sl, std::chrono::milliseconds(100),
                [&] {
                    return !state->pending.empty()
                        || state->closed
                        || reader_done.load(std::memory_order_acquire);
                });

            if (state->closed || (reader_done && state->pending.empty()))
                break;

            if (state->pending.empty())
                continue;

            resp = std::move(state->pending.front());
            state->pending.pop_front();
        }

        if (!stream->Write(resp))
            break;
    }

    reader.join();

    {
        std::lock_guard lock(m_mutex);
        m_streams.erase(chat_id);
    }

    {
        std::lock_guard sl(state->mtx);
        state->closed = true;
        state->cv.notify_all();
    }

    SPDLOG_INFO("StreamChat ended on chat={}", chat_id);
    return grpc::Status::OK;
}

grpc::Status GrpcServiceAdapter::ListExtensions(
    grpc::ServerContext*,
    const rook::v1::ListExtensionsRequest*,
    rook::v1::ListExtensionsResponse* response)
{
    if (!m_extensions) {
        return grpc::Status::OK;
    }

    for (const auto& ext : m_extensions->listInstalled()) {
        auto* e = response->add_extensions();
        e->set_id(ext.name);
        e->set_name(ext.display_name.empty() ? ext.name : ext.display_name);
        e->set_version(ext.version);
        e->set_description(ext.description);
        e->set_enabled(true);
    }

    return grpc::Status::OK;
}

grpc::Status GrpcServiceAdapter::InstallExtension(
    grpc::ServerContext*,
    const rook::v1::InstallExtensionRequest* request,
    rook::v1::InstallExtensionResponse*)
{
    if (!m_extensions) {
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
            "extension port not available");
    }

    bool ok = m_extensions->install(request->id());
    if (!ok) {
        return grpc::Status(grpc::StatusCode::INTERNAL,
            "failed to install extension");
    }

    SPDLOG_INFO("Installed extension from: {}", request->id());
    return grpc::Status::OK;
}

grpc::Status GrpcServiceAdapter::RemoveExtension(
    grpc::ServerContext*,
    const rook::v1::RemoveExtensionRequest* request,
    rook::v1::RemoveExtensionResponse*)
{
    if (!m_extensions) {
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
            "extension port not available");
    }

    bool ok = m_extensions->uninstall(request->id());
    if (!ok) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND,
            "extension not found or could not be removed");
    }

    SPDLOG_INFO("Removed extension: {}", request->id());
    return grpc::Status::OK;
}

grpc::Status GrpcServiceAdapter::GetConfig(
    grpc::ServerContext*,
    const rook::v1::GetConfigRequest*,
    rook::v1::GetConfigResponse* response)
{
    auto json = m_store.loadConfig();

    if (!json.empty() && json != "{}") {
        try {
            auto j = nlohmann::json::parse(json);
            if (j.is_object()) {
                for (auto& [key, value] : j.items()) {
                    auto* entry = response->add_entries();
                    entry->set_key(key);
                    entry->set_value(value.dump());
                }
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("GetConfig parse error: {}", e.what());
        }
    }

    return grpc::Status::OK;
}

grpc::Status GrpcServiceAdapter::UpdateConfig(
    grpc::ServerContext*,
    const rook::v1::UpdateConfigRequest* request,
    rook::v1::UpdateConfigResponse*)
{
    auto existing = m_store.loadConfig();
    nlohmann::json j;

    if (!existing.empty() && existing != "{}") {
        try {
            j = nlohmann::json::parse(existing);
        } catch (...) {
            SPDLOG_WARN("UpdateConfig: existing config invalid, replacing");
        }
    }

    if (!j.is_object())
        j = nlohmann::json::object();

    for (const auto& entry : request->entries()) {
        try {
            j[entry.key()] = nlohmann::json::parse(entry.value());
        } catch (...) {
            j[entry.key()] = entry.value();
        }
    }

    m_store.saveConfig(j.dump(2));
    SPDLOG_INFO("UpdateConfig saved {} entries", request->entries_size());
    return grpc::Status::OK;
}

grpc::Status GrpcServiceAdapter::ListClients(
    grpc::ServerContext*,
    const rook::v1::ListClientsRequest*,
    rook::v1::ListClientsResponse* response)
{
    for (const auto& peer : m_sync.listPeers()) {
        auto* c = response->add_clients();
        c->set_id(peer.node_id);
        c->set_name(peer.node_id);
        c->set_host(peer.host);
        c->set_online(peer.online);
    }
    return grpc::Status::OK;
}

grpc::Status GrpcServiceAdapter::DelegateTask(
    grpc::ServerContext*,
    const rook::v1::DelegateTaskRequest* request,
    rook::v1::DelegateTaskResponse* response)
{
    auto task_id = m_sync.nodeId() + ":"
        + std::to_string(m_sync.now().wall_time_ms);

    m_actor.post(rook::domain::ActorTaskDelegated{
        task_id,
        request->client_id(),
        std::string(
            reinterpret_cast<const char*>(request->payload().data()),
            request->payload().size()),
    });

    response->set_task_id(task_id);
    SPDLOG_INFO("Task delegated: {} → {}", task_id, request->client_id());
    return grpc::Status::OK;
}

grpc::Status GrpcServiceAdapter::PushSyncState(
    grpc::ServerContext*,
    const rook::v1::PushSyncStateRequest* request,
    rook::v1::PushSyncStateResponse*)
{
    const auto& state = request->state();
    m_actor.post(rook::domain::ActorSyncStateReceived{
        state.node_id(),
        {state.crdt_data().begin(), state.crdt_data().end()},
    });

    auto hlc = rook::sync::HlcTimestamp{
        state.hlc_wall_time_ms(),
        state.hlc_logical_counter(),
        state.node_id(),
    };
    m_sync.observe(hlc);

    SPDLOG_DEBUG("PushSyncState from {}", state.node_id());
    return grpc::Status::OK;
}

grpc::Status GrpcServiceAdapter::PullSyncState(
    grpc::ServerContext*,
    const rook::v1::PullSyncStateRequest* request,
    rook::v1::PullSyncStateResponse* response)
{
    (void)request;
    auto data = m_sync.serializeFullState();
    auto ts = m_sync.now();

    auto* state = response->mutable_state();
    state->set_node_id(m_sync.nodeId());
    state->set_hlc_wall_time_ms(ts.wall_time_ms);
    state->set_hlc_logical_counter(ts.logical_counter);
    state->set_crdt_data(
        reinterpret_cast<const char*>(data.data()),
        data.size());

    SPDLOG_DEBUG("PullSyncState → {} ({} B)",
        request->node_id(), data.size());
    return grpc::Status::OK;
}

grpc::Status GrpcServiceAdapter::CreateTenant(
    grpc::ServerContext*,
    const rook::v1::CreateTenantRequest*,
    rook::v1::CreateTenantResponse*)
{
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
        "CreateTenant requires multi-tenant mode (--jwt-secret)");
}

grpc::Status GrpcServiceAdapter::ListTenants(
    grpc::ServerContext*,
    const rook::v1::ListTenantsRequest*,
    rook::v1::ListTenantsResponse*)
{
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
        "ListTenants requires multi-tenant mode (--jwt-secret)");
}

grpc::Status GrpcServiceAdapter::DeleteTenant(
    grpc::ServerContext*,
    const rook::v1::DeleteTenantRequest*,
    rook::v1::DeleteTenantResponse*)
{
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
        "DeleteTenant requires multi-tenant mode (--jwt-secret)");
}

} // namespace rook::adapters::server
