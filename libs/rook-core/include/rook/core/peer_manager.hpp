#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <functional>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include "rook/ports/llm_port.hpp"
#include "rook/ports/store_port.hpp"
#include "rook/ports/extension_port.hpp"

namespace rook::adapters::client {
class GrpcClientLlmPort;
class GrpcClientStorePort;
class GrpcClientExtensionPort;
}

namespace rook::core {

struct PeerConfig {
    std::string address;
    std::string token;
    bool sync_settings = true;
    bool sync_extensions = true;
    bool sync_chats = false;
    bool use_remote_llm = false;
};

struct Peer {
    std::string node_id;
    PeerConfig config;
    bool connected = false;

    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<rook::adapters::client::GrpcClientLlmPort> llm;
    std::unique_ptr<rook::adapters::client::GrpcClientStorePort> store;
    std::unique_ptr<rook::adapters::client::GrpcClientExtensionPort> extensions;

    Peer();
    ~Peer();
    Peer(Peer&&) noexcept;
    Peer& operator=(Peer&&) noexcept;
};

class PeerManager {
public:
    PeerManager();
    ~PeerManager();

    PeerManager(const PeerManager&) = delete;
    PeerManager& operator=(const PeerManager&) = delete;

    void addPeer(PeerConfig config);
    void removePeer(std::string_view address);
    [[nodiscard]] std::vector<const Peer*> peers() const;
    [[nodiscard]] Peer* findPeer(std::string_view address);

    void connect(std::string_view address);
    void disconnect(std::string_view address);

    [[nodiscard]] rook::ports::LlmPort* activeLlm() const;
    void setActiveLlm(std::string_view address);
    [[nodiscard]] bool isRemoteLlmActive() const;

    [[nodiscard]] rook::ports::LlmPort& localLlm() { return *m_local_llm; }
    [[nodiscard]] rook::ports::StorePort& localStore() { return *m_local_store; }

    void setLocalLlm(rook::ports::LlmPort* llm) { m_local_llm = llm; }
    void setLocalStore(rook::ports::StorePort* store) { m_local_store = store; }

private:
    std::vector<Peer> m_peers;
    mutable std::mutex m_mutex;

    std::string m_active_llm_peer;

    rook::ports::LlmPort* m_local_llm = nullptr;
    rook::ports::StorePort* m_local_store = nullptr;
};

std::vector<PeerConfig> parsePeersConfig(std::string_view json);
std::string serializePeersConfig(const std::vector<PeerConfig>& peers);

} // namespace rook::core
