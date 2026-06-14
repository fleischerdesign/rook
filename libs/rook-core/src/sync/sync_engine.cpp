#include "rook/sync/sync_engine.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace rook::sync {

using json = nlohmann::json;

SyncEngine::SyncEngine(std::string node_id)
    : m_clock(std::move(node_id))
{
}

void SyncEngine::putSetting(const std::string& key, const std::string& value)
{
    m_settings.put(key, value, m_clock.now());
    if (onChanged) onChanged();
}

void SyncEngine::removeSetting(const std::string& key)
{
    m_settings.remove(key, m_clock.now());
    if (onChanged) onChanged();
}

std::optional<std::string> SyncEngine::getSetting(const std::string& key) const
{
    return m_settings.get(key);
}

std::vector<std::pair<std::string, std::string>> SyncEngine::allSettings() const
{
    return m_settings.snapshot();
}

void SyncEngine::addExtension(const ExtensionInfo& ext)
{
    m_extensions.add(ext, m_clock.now());
    if (onChanged) onChanged();
}

void SyncEngine::removeExtension(const ExtensionInfo& ext)
{
    m_extensions.remove(ext);
    if (onChanged) onChanged();
}

std::vector<ExtensionInfo> SyncEngine::listExtensions() const
{
    return m_extensions.elements();
}

void SyncEngine::insertChatMessage(
    const std::string& chat_id,
    std::string_view left_origin,
    std::string_view right_origin,
    const ChatMessage& msg)
{
    m_chats[chat_id].insert(left_origin, right_origin, msg, m_clock.now());
    if (onChanged) onChanged();
}

void SyncEngine::removeChatMessage(const std::string& chat_id,
                                    std::string_view msg_id)
{
    auto it = m_chats.find(chat_id);
    if (it != m_chats.end()) {
        it->second.remove(msg_id);
        if (onChanged) onChanged();
    }
}

std::vector<ChatMessage> SyncEngine::chatSnapshot(const std::string& chat_id) const
{
    auto it = m_chats.find(chat_id);
    if (it != m_chats.end())
        return it->second.snapshot();
    return {};
}

void SyncEngine::clear()
{
    m_settings.clear();
    m_extensions.clear();
    m_chats.clear();
    m_peers = GSet<PeerInfo>{};
}

static json serializeHlc(const HlcTimestamp& ts)
{
    return {
        {"wall_time_ms", ts.wall_time_ms},
        {"logical_counter", ts.logical_counter},
        {"node_id", ts.node_id},
    };
}

static HlcTimestamp deserializeHlc(const json& j)
{
    HlcTimestamp ts;
    ts.wall_time_ms = j.at("wall_time_ms").get<int64_t>();
    ts.logical_counter = j.at("logical_counter").get<int32_t>();
    ts.node_id = j.at("node_id").get<std::string>();
    return ts;
}

static json serializeElementTag(const AwSet<ExtensionInfo>::ElementTag& tag)
{
    return {
        {"unique_id", tag.unique_id},
        {"element", {
            {"name", tag.element.name},
            {"version", tag.element.version},
        }},
        {"tag", serializeHlc(tag.tag)},
    };
}

static AwSet<ExtensionInfo>::ElementTag deserializeElementTag(const json& j)
{
    AwSet<ExtensionInfo>::ElementTag tag;
    tag.unique_id = j.at("unique_id").get<std::string>();
    tag.element.name = j.at("element").at("name").get<std::string>();
    tag.element.version = j.at("element").at("version").get<std::string>();
    tag.tag = deserializeHlc(j.at("tag"));
    return tag;
}

[[maybe_unused]] static json serializeMessage(const ChatMessage& m)
{
    return {
        {"role", m.role},
        {"content", m.content},
        {"tool_call_id", m.tool_call_id},
        {"tool_name", m.tool_name},
        {"tool_calls", m.tool_calls},
    };
}

[[maybe_unused]] static ChatMessage deserializeMessage(const json& j)
{
    ChatMessage m;
    m.role = j.value("role", "");
    m.content = j.value("content", "");
    m.tool_call_id = j.value("tool_call_id", "");
    m.tool_name = j.value("tool_name", "");
    m.tool_calls = j.value("tool_calls", "");
    return m;
}

std::vector<uint8_t> SyncEngine::serializeFullState() const
{
    json root;

    json settings_arr = json::array();
    for (const auto& entry : m_settings.allEntries()) {
        json e;
        e["key"] = entry.key;
        e["value"] = entry.value;
        e["timestamp"] = serializeHlc(entry.timestamp);
        e["tombstone"] = entry.tombstone;
        settings_arr.push_back(std::move(e));
    }
    root["settings"] = settings_arr;

    auto ext_state = m_extensions.fullState();
    json ext_added = json::array();
    for (const auto& tag : ext_state.added) {
        ext_added.push_back(serializeElementTag(tag));
    }
    json ext_removed = json::array();
    for (const auto& tag : ext_state.removed) {
        ext_removed.push_back(serializeElementTag(tag));
    }
    root["extensions"] = {
        {"added", std::move(ext_added)},
        {"removed", std::move(ext_removed)},
    };

    json peers_arr = json::array();
    for (const auto& peer : m_peers.elements()) {
        peers_arr.push_back({
            {"node_id", peer.node_id},
            {"host", peer.host},
            {"online", peer.online},
        });
    }
    root["peers"] = peers_arr;

    std::string json_str = root.dump();
    return {json_str.begin(), json_str.end()};
}

void SyncEngine::mergeFullState(const std::vector<uint8_t>& data)
{
    if (data.empty())
        return;

    std::string json_str(data.begin(), data.end());
    json root;

    try {
        root = json::parse(json_str);
    } catch (const json::parse_error&) {
        return;
    }

    if (root.contains("settings") && root["settings"].is_array()) {
        for (const auto& e : root["settings"]) {
            std::string key = e.at("key").get<std::string>();
            std::string value = e.at("value").get<std::string>();
            auto ts = deserializeHlc(e.at("timestamp"));
            bool tombstone = e.value("tombstone", false);
            m_settings.putRaw(key, std::move(value), ts, tombstone);
        }
    }

    if (root.contains("extensions") && root["extensions"].is_object()) {
        auto& ext = root["extensions"];
        if (ext.contains("added") && ext["added"].is_array()) {
            for (const auto& tag_json : ext["added"]) {
                m_extensions.addRaw(deserializeElementTag(tag_json));
            }
        }
        if (ext.contains("removed") && ext["removed"].is_array()) {
            for (const auto& tag_json : ext["removed"]) {
                m_extensions.removeRaw(deserializeElementTag(tag_json));
            }
        }
    }

    if (root.contains("peers") && root["peers"].is_array()) {
        for (const auto& p : root["peers"]) {
            PeerInfo peer;
            peer.node_id = p.at("node_id").get<std::string>();
            peer.host = p.value("host", "");
            peer.online = p.value("online", false);
            m_peers.add(std::move(peer));
        }
    }
}

} // namespace rook::sync
