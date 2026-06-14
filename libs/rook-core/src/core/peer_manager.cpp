#include "rook/core/peer_manager.hpp"
#include "rook/adapters/client/grpc_client_llm_port.hpp"
#include "rook/adapters/client/grpc_client_store_port.hpp"
#include "rook/adapters/client/grpc_client_extension_port.hpp"
#include "service.grpc.pb.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace rook::core {

Peer::Peer() = default;
Peer::~Peer() = default;
Peer::Peer(Peer&&) noexcept = default;
Peer& Peer::operator=(Peer&&) noexcept = default;

PeerManager::PeerManager() = default;
PeerManager::~PeerManager() = default;

void PeerManager::addPeer(PeerConfig config)
{
    std::lock_guard lock(m_mutex);

    auto it = std::find_if(m_peers.begin(), m_peers.end(),
        [&](const Peer& p) { return p.config.address == config.address; });

    if (it != m_peers.end()) {
        it->config = std::move(config);
        return;
    }

    m_peers.push_back({});
    m_peers.back().config = std::move(config);
}

void PeerManager::removePeer(std::string_view address)
{
    std::lock_guard lock(m_mutex);
    std::erase_if(m_peers, [&](const Peer& p) {
        return p.config.address == address;
    });
}

std::vector<const Peer*> PeerManager::peers() const
{
    std::lock_guard lock(m_mutex);
    std::vector<const Peer*> result;
    for (const auto& p : m_peers)
        result.push_back(&p);
    return result;
}

Peer* PeerManager::findPeer(std::string_view address)
{
    std::lock_guard lock(m_mutex);
    for (auto& p : m_peers) {
        if (p.config.address == address)
            return &p;
    }
    return nullptr;
}

void PeerManager::connect(std::string_view address)
{
    Peer* peer = findPeer(address);
    if (!peer) {
        SPDLOG_WARN("PeerManager: cannot connect unknown peer {}", address);
        return;
    }

    std::string target = std::string(address);
    grpc::ChannelArguments args;
    if (!peer->config.token.empty()) {
        args.SetString("authorization", "Bearer " + peer->config.token);
    }

    peer->channel = grpc::CreateCustomChannel(
        target, grpc::InsecureChannelCredentials(), args);

    peer->llm = std::make_unique<rook::adapters::client::GrpcClientLlmPort>(
        peer->channel);
    peer->store = std::make_unique<rook::adapters::client::GrpcClientStorePort>(
        peer->channel);
    peer->extensions = std::make_unique<rook::adapters::client::GrpcClientExtensionPort>(
        peer->channel);

    peer->connected = true;
    SPDLOG_INFO("PeerManager: connected to {}", address);
}

void PeerManager::disconnect(std::string_view address)
{
    Peer* peer = findPeer(address);
    if (!peer) return;

    peer->connected = false;
    peer->llm.reset();
    peer->store.reset();
    peer->extensions.reset();
    peer->channel.reset();
    SPDLOG_INFO("PeerManager: disconnected from {}", address);
}

bool PeerManager::pushToPeer(std::string_view address,
                             const std::vector<std::uint8_t>& data)
{
    Peer* peer = findPeer(address);
    if (!peer || !peer->connected) return false;

    rook::v1::RookService::Stub stub(peer->channel);

    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now()
        + std::chrono::seconds(5));

    rook::v1::PushSyncStateRequest req;
    req.mutable_state()->set_node_id("");
    req.mutable_state()->set_crdt_data(
        reinterpret_cast<const char*>(data.data()), data.size());

    rook::v1::PushSyncStateResponse resp;
    auto status = stub.PushSyncState(&ctx, req, &resp);

    return status.ok();
}

std::vector<std::uint8_t> PeerManager::pullFromPeer(
    std::string_view address)
{
    Peer* peer = findPeer(address);
    if (!peer || !peer->connected) return {};

    rook::v1::RookService::Stub stub(peer->channel);

    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now()
        + std::chrono::seconds(10));

    rook::v1::PullSyncStateRequest req;
    req.set_node_id("");

    rook::v1::PullSyncStateResponse resp;
    auto status = stub.PullSyncState(&ctx, req, &resp);

    if (!status.ok()) return {};

    auto& data = resp.state().crdt_data();
    return {data.begin(), data.end()};
}

std::vector<PeerConfig> parsePeersConfig(std::string_view json)
{
    std::vector<PeerConfig> peers;
    if (json.empty() || json == "{}") return peers;

    try {
        auto j = nlohmann::json::parse(json);
        if (j.contains("peers") && j["peers"].is_array()) {
            for (const auto& p : j["peers"]) {
                PeerConfig cfg;
                cfg.address = p.value("address", "");
                cfg.token = p.value("token", "");
                cfg.sync_settings = p.value("sync_settings", true);
                cfg.sync_extensions = p.value("sync_extensions", true);
                cfg.sync_chats = p.value("sync_chats", false);
                if (!cfg.address.empty())
                    peers.push_back(std::move(cfg));
            }
        }
    } catch (...) {}

    return peers;
}

std::string serializePeersConfig(const std::vector<PeerConfig>& peers)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : peers) {
        nlohmann::json obj;
        obj["address"] = p.address;
        obj["token"] = p.token;
        obj["sync_settings"] = p.sync_settings;
        obj["sync_extensions"] = p.sync_extensions;
        obj["sync_chats"] = p.sync_chats;
        arr.push_back(std::move(obj));
    }
    return arr.dump(2);
}

} // namespace rook::core
