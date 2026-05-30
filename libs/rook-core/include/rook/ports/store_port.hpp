#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>

namespace rook::ports {

struct ChatRecord {
    std::string id;
    std::string title;
    std::string model;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    std::string messages_json;
};

class StorePort {
public:
    virtual ~StorePort() = default;

    virtual void saveChat(const ChatRecord& record) = 0;

    virtual std::optional<ChatRecord> loadChat(std::string_view id) = 0;

    virtual std::vector<ChatRecord> listChats() = 0;

    virtual void deleteChat(std::string_view id) = 0;

    virtual std::string loadConfig() = 0;

    virtual void saveConfig(std::string_view json) = 0;
};

} // namespace rook::ports
