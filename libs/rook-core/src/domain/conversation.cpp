#include "rook/domain/conversation.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

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
        m_active_id = id;
        return *it;
    }

    spdlog::warn("Conversation not found: {}", id);
    return create("New Chat", "default");
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

    if (m_store) {
        saveActiveConversation(*m_store);
    }
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

std::vector<ports::LlmMessage> ConversationManager::buildLlmMessages(
    std::string_view conv_id
) const {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](const auto& c) { return c.id == conv_id; });

    std::vector<ports::LlmMessage> result;

    if (it == m_conversations.end()) return result;

    for (const auto& msg : it->messages) {
        if (msg.role == "tool") continue;
        result.push_back({msg.role, msg.content});
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

std::optional<Conversation> ConversationManager::active() const {
    auto it = std::ranges::find_if(m_conversations,
        [this](const auto& c) { return c.id == m_active_id; });

    if (it != m_conversations.end()) return *it;
    return std::nullopt;
}

void ConversationManager::setActive(std::string_view id) {
    m_active_id = id;
}

void ConversationManager::start(EventBus& bus, ports::StorePort* store) {
    m_bus = &bus;
    m_store = store;

    m_chat_created_handler = bus.subscribe<ChatCreated>(
        [this](const ChatCreated& event) { onChatCreated(event); });

    m_chat_deleted_handler = bus.subscribe<ChatDeleted>(
        [this](const ChatDeleted& event) { onChatDeleted(event); });
}

void ConversationManager::onChatCreated(const ChatCreated& event) {
    if (!event.chat_id.empty()) return;

    auto conv = create("New Chat", "default");

    if (m_store) {
        saveActiveConversation(*m_store);
    }

    if (m_bus) {
        m_bus->publish(ChatCreated{
            .chat_id = conv.id
        });
        m_bus->publish(ChatSelected{
            .chat_id = conv.id
        });
    }
}

void ConversationManager::onChatDeleted(const ChatDeleted& event) {
    if (m_store) {
        m_store->deleteChat(event.chat_id);
    }
    remove(event.chat_id);
}

std::string ConversationManager::generateTitle(const Conversation& conv) const {
    if (!conv.messages.empty()) {
        auto& msg = conv.messages[0];
        auto title = msg.content.substr(0, 50);
        if (msg.content.size() > 50) title += "...";
        return title;
    }
    return "New Chat";
}

void ConversationManager::setTitle(std::string_view conv_id, std::string_view title) {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });

    if (it == m_conversations.end()) return;

    it->title = title;

    if (m_bus) {
        m_bus->publish(ChatUpdated{
            .chat_id = std::string(conv_id),
            .title = std::string(title),
        });
    }

    if (m_store) {
        saveActiveConversation(*m_store);
    }
}

void ConversationManager::setModel(std::string_view conv_id, std::string_view model) {
    auto it = std::ranges::find_if(m_conversations,
        [conv_id](auto& c) { return c.id == conv_id; });

    if (it == m_conversations.end()) return;
    it->model = model;
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

        if (!opt->messages_json.empty()) {
            try {
                auto j = nlohmann::json::parse(opt->messages_json);
                for (const auto& mj : j) {
                    ChatMessage msg;
                    msg.id = mj.value("id", generateId());
                    msg.role = mj.value("role", "user");
                    msg.content = mj.value("content", "");
                    msg.timestamp = std::chrono::system_clock::now();
                    msg.has_tool_calls = mj.value("has_tool_calls", false);
                    msg.tool_call_id = mj.value("tool_call_id", "");
                    conv.messages.push_back(std::move(msg));
                }
            } catch (const std::exception& e) {
                spdlog::error("Failed to parse messages for {}: {}", conv.id, e.what());
            }
        }

        m_conversations.push_back(std::move(conv));
    }

    spdlog::info("Loaded {} conversations from store", m_conversations.size());
}

void ConversationManager::saveActiveConversation(ports::StorePort& store) {
    auto conv = active();
    if (!conv) return;

    ports::ChatRecord record;
    record.id = conv->id;
    record.title = conv->title;
    record.model = conv->model;
    record.created_at = conv->created_at;
    record.updated_at = conv->updated_at;

    nlohmann::json messages = nlohmann::json::array();
    for (const auto& msg : conv->messages) {
        nlohmann::json mj;
        mj["id"] = msg.id;
        mj["role"] = msg.role;
        mj["content"] = msg.content;
        mj["has_tool_calls"] = msg.has_tool_calls;
        mj["tool_call_id"] = msg.tool_call_id;
        messages.push_back(mj);
    }

    record.messages_json = messages.dump();
    store.saveChat(record);
}

} // namespace rook::domain
