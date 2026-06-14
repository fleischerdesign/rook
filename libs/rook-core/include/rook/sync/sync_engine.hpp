#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>
#include <deque>
#include <functional>
#include "rook/sync/hlc.hpp"
#include "rook/sync/crdt_gset.hpp"
#include "rook/sync/crdt_lww_map.hpp"
#include "rook/sync/crdt_awset.hpp"
#include "rook/sync/crdt_yata.hpp"

namespace rook::sync {

struct PeerInfo {
    std::string node_id;
    std::string host;
    bool online = false;

    bool operator==(const PeerInfo& other) const
    {
        return node_id == other.node_id;
    }
    bool operator<(const PeerInfo& other) const
    {
        return node_id < other.node_id;
    }
};

struct ExtensionInfo {
    std::string name;
    std::string version;

    bool operator==(const ExtensionInfo& other) const
    {
        return name == other.name && version == other.version;
    }
    bool operator<(const ExtensionInfo& other) const
    {
        if (name != other.name) return name < other.name;
        return version < other.version;
    }
};

struct ChatMessage {
    std::string role;
    std::string content;
    std::string tool_call_id;
    std::string tool_name;
    std::string tool_calls;

    bool operator==(const ChatMessage& other) const
    {
        return role == other.role
            && content == other.content
            && tool_call_id == other.tool_call_id
            && tool_name == other.tool_name
            && tool_calls == other.tool_calls;
    }
};

} // namespace rook::sync

namespace std {

template <>
struct hash<rook::sync::PeerInfo> {
    size_t operator()(const rook::sync::PeerInfo& p) const noexcept
    {
        return hash<string>{}(p.node_id);
    }
};

template <>
struct hash<rook::sync::ExtensionInfo> {
    size_t operator()(const rook::sync::ExtensionInfo& e) const noexcept
    {
        return hash<string>{}(e.name) ^ (hash<string>{}(e.version) << 1);
    }
};

} // namespace std

namespace rook::sync {

class SyncEngine {
public:
    explicit SyncEngine(std::string node_id);

    SyncEngine(const SyncEngine&) = delete;
    SyncEngine& operator=(const SyncEngine&) = delete;

    [[nodiscard]] const std::string& nodeId() const { return m_clock.nodeId(); }
    [[nodiscard]] HlcTimestamp now() { return m_clock.now(); }
    void observe(const HlcTimestamp& remote) { m_clock.observe(remote); }

    void putSetting(const std::string& key, const std::string& value);
    void removeSetting(const std::string& key);
    [[nodiscard]] std::optional<std::string> getSetting(const std::string& key) const;
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> allSettings() const;

    void addExtension(const ExtensionInfo& ext);
    void removeExtension(const ExtensionInfo& ext);
    [[nodiscard]] std::vector<ExtensionInfo> listExtensions() const;

    void insertChatMessage(const std::string& chat_id,
                           std::string_view left_origin,
                           std::string_view right_origin,
                           const ChatMessage& msg);
    void removeChatMessage(const std::string& chat_id,
                           std::string_view msg_id);
    [[nodiscard]] std::vector<ChatMessage> chatSnapshot(
        const std::string& chat_id) const;

    void addPeer(const PeerInfo& peer) { m_peers.add(peer); if (onChanged) onChanged(); }
    [[nodiscard]] std::vector<PeerInfo> listPeers() const { return m_peers.elements(); }

    [[nodiscard]] std::vector<uint8_t> serializeFullState() const;
    void mergeFullState(const std::vector<uint8_t>& data);

    std::function<void()> onChanged;

    void clear();

private:
    HybridLogicalClock m_clock;

    LwwMap<std::string, std::string> m_settings;
    AwSet<ExtensionInfo> m_extensions;
    std::unordered_map<std::string, YataSequence<ChatMessage>> m_chats;
    GSet<PeerInfo> m_peers;
};

} // namespace rook::sync
