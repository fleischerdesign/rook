#include "rook/domain/conversation.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ranges>

namespace rook::domain {

namespace {

std::string generateId() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    auto rnd = dis(gen);

    std::ostringstream oss;
    oss << std::hex << now << "-" << rnd;
    return oss.str();
}

} // namespace

Conversation ConversationManager::open(std::string_view id) {
    auto it = std::ranges::find_if(m_conversations,
        [id](const auto& c) { return c.id == id; });

    if (it != m_conversations.end()) {
        return *it;
    }

    spdlog::warn("Conversation not found: {}", id);
    return Conversation{};
}

std::vector<Conversation> ConversationManager::list() const {
    return m_conversations;
}

Conversation ConversationManager::create(std::string_view title, std::string_view model) {
    Conversation conv;
    conv.id = generateId();
    conv.title = std::string(title);
    conv.model = std::string(model);
    conv.created_at = std::chrono::system_clock::now();
    conv.updated_at = conv.created_at;

    m_conversations.push_back(conv);
    m_active_id = conv.id;

    spdlog::info("Created conversation: {} ({})", conv.id, conv.title);
    return conv;
}

std::optional<Conversation> ConversationManager::openEphemeral(std::string_view model_hint) {
    Conversation conv;
    conv.id = generateId();
    conv.title = "[Voice]";
    conv.model = std::string(model_hint);
    conv.created_at = std::chrono::system_clock::now();
    conv.updated_at = conv.created_at;
    conv.ephemeral = true;

    m_conversations.push_back(conv);
    spdlog::debug("Created ephemeral conversation: {}", conv.id);
    return conv;
}

void ConversationManager::remove(std::string_view id) {
    std::erase_if(m_conversations,
        [id](const auto& c) { return c.id == id; });

    if (m_active_id == id) {
        m_active_id.clear();
    }
}

void ConversationManager::close(std::string_view id) {
    if (m_active_id == id) {
        m_active_id.clear();
    }
}

void ConversationManager::addMessage(std::string_view conv_id, ChatMessage message) {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });

    if (it == m_conversations.end()) return;

    message.id = generateId();
    message.timestamp = std::chrono::system_clock::now();

    it->messages.push_back(std::move(message));
    it->updated_at = std::chrono::system_clock::now();
}

void ConversationManager::updateAssistantChunk(
    std::string_view conv_id,
    std::string_view chunk
) {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });

    if (it == m_conversations.end()) return;

    if (it->messages.empty() || it->messages.back().role != "assistant") {
        ChatMessage msg;
        msg.id = generateId();
        msg.role = "assistant";
        msg.timestamp = std::chrono::system_clock::now();
        it->messages.push_back(std::move(msg));
    }

    it->messages.back().content.append(chunk);
}

void ConversationManager::updateReasoningChunk(
    std::string_view conv_id,
    std::string_view chunk
) {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });

    if (it == m_conversations.end()) return;

    if (it->messages.empty() || it->messages.back().role != "assistant") {
        ChatMessage msg;
        msg.id = generateId();
        msg.role = "assistant";
        msg.timestamp = std::chrono::system_clock::now();
        it->messages.push_back(std::move(msg));
    }

    it->messages.back().reasoning_content.append(chunk);
}

void ConversationManager::setAssistantToolCalls(std::string_view conv_id, std::string json) {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });
    if (it == m_conversations.end()) return;

    for (auto msg_it = it->messages.rbegin(); msg_it != it->messages.rend(); ++msg_it) {
        if (msg_it->role == "assistant") {
            msg_it->has_tool_calls = true;
            msg_it->tool_calls_json = std::move(json);
            return;
        }
    }

    ChatMessage msg;
    msg.id = generateId();
    msg.role = "assistant";
    msg.has_tool_calls = true;
    msg.tool_calls_json = std::move(json);
    msg.timestamp = std::chrono::system_clock::now();
    it->messages.push_back(std::move(msg));
}

std::vector<ports::LlmMessage> ConversationManager::buildLlmMessages(
    std::string_view conv_id
) const {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](const auto& c) { return c.id == conv_id; });

    std::vector<ports::LlmMessage> result;

    if (it == m_conversations.end()) return result;

    for (const auto& msg : it->messages) {
        ports::LlmMessage lm;
        lm.role = msg.role;
        lm.content = msg.content;
        lm.tool_call_id = msg.tool_call_id;
        lm.tool_name = msg.tool_name;
        lm.tool_calls = msg.tool_calls_json;
        result.push_back(std::move(lm));
    }

    return result;
}

int32_t ConversationManager::estimateTokens(std::string_view conv_id) const {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](const auto& c) { return c.id == conv_id; });

    if (it == m_conversations.end()) return 0;

    int32_t total = 0;
    for (const auto& msg : it->messages) {
        total += static_cast<int32_t>(msg.content.size()) / 4;
    }

    return total;
}

std::string* ConversationManager::lastResponse(std::string_view conv_id)
{
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });
    if (it == m_conversations.end()) return nullptr;

    for (auto msg = it->messages.rbegin(); msg != it->messages.rend(); ++msg) {
        if (msg->role == "assistant")
            return &msg->content;
    }
    return nullptr;
}

std::optional<Conversation> ConversationManager::active() const {
    auto it = std::ranges::find_if(m_conversations,
        [this](const auto& c) { return c.id == m_active_id; });

    if (it != m_conversations.end()) return *it;
    return std::nullopt;
}

void ConversationManager::setActive(std::string_view id) {
    m_active_id = id;
}

void ConversationManager::setTitle(std::string_view conv_id, std::string_view title) {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });

    if (it == m_conversations.end()) return;
    it->title = title;
    it->title_is_manual = true;
}

void ConversationManager::setModel(std::string_view conv_id, std::string_view model) {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });

    if (it == m_conversations.end()) return;
    it->model = model;
}

void ConversationManager::togglePin(std::string_view conv_id) {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });
    if (it == m_conversations.end()) return;

    it->pinned = !it->pinned;
    it->pinned_at = it->pinned
        ? std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count()
        : 0;
}

bool ConversationManager::isPinned(std::string_view conv_id) const {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](const auto& c) { return c.id == conv_id; });
    return it != m_conversations.end() && it->pinned;
}

bool ConversationManager::isToolWhitelisted(std::string_view conv_id,
                                             std::string_view tool) const {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](const auto& c) { return c.id == conv_id; });
    if (it == m_conversations.end()) return false;
    return std::ranges::find(it->whitelisted_tools, tool)
        != it->whitelisted_tools.end();
}

void ConversationManager::addWhitelistedTool(std::string_view conv_id,
                                              std::string_view tool) {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });
    if (it == m_conversations.end()) return;
    auto t = std::string(tool);
    if (std::ranges::find(it->whitelisted_tools, t)
        == it->whitelisted_tools.end())
        it->whitelisted_tools.push_back(std::move(t));
}

void ConversationManager::loadFromStore(ports::StorePort& store) {
    auto chat_ids = store.listChats();
    for (const auto& record : chat_ids) {
        auto opt = store.loadChat(record.id);
        if (!opt) continue;

        Conversation conv;
        conv.id = opt->id;
        conv.title = opt->title;
        conv.model = opt->model;
        conv.created_at = opt->created_at;
        conv.updated_at = opt->updated_at;
        conv.pinned = opt->pinned;
        conv.pinned_at = opt->pinned_at;

        if (!opt->messages_json.empty()) {
            try {
                auto j = nlohmann::json::parse(opt->messages_json);
                for (const auto& mj : j) {
                    ChatMessage msg;
                    msg.id = mj.value("id", generateId());
                    msg.role = mj.value("role", "user");
                    msg.content = mj.value("content", "");
                    msg.reasoning_content = mj.value("reasoning_content", "");
                    msg.timestamp = std::chrono::system_clock::now();
                    msg.has_tool_calls = mj.value("has_tool_calls", false);
                    msg.tool_call_id = mj.value("tool_call_id", "");
                    msg.tool_name = mj.value("tool_name", "");
                    msg.tool_calls_json = mj.value("tool_calls_json", "");
                    conv.messages.push_back(std::move(msg));
                }
            } catch (const std::exception& e) {
                spdlog::error("Failed to parse messages for {}: {}", conv.id, e.what());
            }
        }

        if (!opt->active_skill_ids_json.empty()) {
            try {
                auto j = nlohmann::json::parse(opt->active_skill_ids_json);
                if (j.is_array()) {
                    for (auto& s : j) {
                        if (s.is_string())
                            conv.active_skill_ids.push_back(s.get<std::string>());
                    }
                }
            } catch (...) {}
        }

        if (!opt->whitelisted_tools_json.empty()) {
            try {
                auto j = nlohmann::json::parse(opt->whitelisted_tools_json);
                if (j.is_array()) {
                    for (auto& t : j)
                        if (t.is_string())
                            conv.whitelisted_tools.push_back(t.get<std::string>());
                }
            } catch (...) {}
        }

        m_conversations.push_back(std::move(conv));
    }

    spdlog::info("Loaded {} conversations from store", m_conversations.size());
}

void ConversationManager::saveActiveConversation() {
    if (!m_store) return;

    auto it = std::ranges::find_if(m_conversations,
        [this](const auto& c) { return c.id == m_active_id; });

    if (it == m_conversations.end()) return;

    const auto& conv = *it;

    ports::ChatRecord record;
    record.id = conv.id;
    record.title = conv.title;
    record.model = conv.model;
    record.created_at = conv.created_at;
    record.updated_at = conv.updated_at;

    nlohmann::json messages = nlohmann::json::array();
    for (const auto& msg : conv.messages) {
        nlohmann::json mj;
        mj["id"] = msg.id;
        mj["role"] = msg.role;
        mj["content"] = msg.content;
        mj["reasoning_content"] = msg.reasoning_content;
        mj["has_tool_calls"] = msg.has_tool_calls;
        mj["tool_call_id"] = msg.tool_call_id;
        mj["tool_name"] = msg.tool_name;
        mj["tool_calls_json"] = msg.tool_calls_json;
        messages.push_back(mj);
    }

    record.messages_json = messages.dump();
    record.pinned = conv.pinned;
    record.pinned_at = conv.pinned_at;

    nlohmann::json wl = nlohmann::json::array();
    for (auto& t : conv.whitelisted_tools) wl.push_back(t);
    record.whitelisted_tools_json = wl.dump();

    m_store->saveChat(record);
}

void ConversationManager::setSystemMessage(
    std::string_view conv_id,
    std::string_view content)
{
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });
    if (it == m_conversations.end()) return;

    ChatMessage sys;
    sys.role = "system";
    sys.content = std::string(content);
    sys.id = generateId();
    sys.timestamp = std::chrono::system_clock::now();
    it->messages.insert(it->messages.begin(), std::move(sys));
}

void ConversationManager::updateSystemMessage(
    std::string_view conv_id,
    std::string_view content)
{
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });
    if (it == m_conversations.end()) return;

    for (auto& msg : it->messages) {
        if (msg.role == "system") {
            msg.content = std::string(content);
            return;
        }
    }

    setSystemMessage(conv_id, content);
}

void ConversationManager::setActiveSkillIds(
    std::string_view conv_id,
    std::vector<std::string> ids)
{
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });
    if (it == m_conversations.end()) return;
    it->active_skill_ids = std::move(ids);
}

std::vector<std::string> ConversationManager::activeSkillIds(
    std::string_view conv_id) const
{
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });
    if (it != m_conversations.end()) return it->active_skill_ids;
    return {};
}

} // namespace rook::domain
