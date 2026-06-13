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
}

void SyncEngine::removeSetting(const std::string& key)
{
    m_settings.remove(key, m_clock.now());
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
}

void SyncEngine::removeExtension(const ExtensionInfo& ext)
{
    m_extensions.remove(ext);
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
    m_chats[chat_id].insert(left_origin, right_origin,
        msg, m_clock.now());
}

void SyncEngine::removeChatMessage(
    const std::string& chat_id,
    std::string_view msg_id)
{
    auto it = m_chats.find(chat_id);
    if (it != m_chats.end())
        it->second.remove(msg_id);
}

std::vector<ChatMessage> SyncEngine::chatSnapshot(
    const std::string& chat_id) const
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

static json serializeMessage(const ChatMessage& m)
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
    for (const auto& [key, value] : m_settings.snapshot()) {
        json settings_obj;
        settings_obj["key"] = key;
        settings_obj["value"] = value;
        settings_arr.push_back(std::move(settings_obj));
    }
    root["settings"] = settings_arr;

    json ext_arr = json::array();
    for (const auto& ext : m_extensions.elements()) {
        ext_arr.push_back({
            {"name", ext.name},
            {"version", ext.version},
        });
    }
    root["extensions"] = ext_arr;

    json chats_obj = json::object();
    for (const auto& [chat_id, _] : m_chats) {
        auto msgs = chatSnapshot(chat_id);
        json msgs_arr = json::array();
        for (const auto& msg : msgs) {
            msgs_arr.push_back(serializeMessage(msg));
        }
        chats_obj[chat_id] = msgs_arr;
    }
    root["chats"] = chats_obj;

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
