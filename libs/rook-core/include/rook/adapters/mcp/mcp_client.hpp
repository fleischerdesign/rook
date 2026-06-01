#pragma once

#include "rook/adapters/mcp/mcp_transport.hpp"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace rook::adapters::mcp {

class McpClient {
public:
    using NotificationHandler =
        std::function<void(std::string_view method, nlohmann::json params)>;

    explicit McpClient(std::unique_ptr<McpTransport> transport);
    ~McpClient();

    void setNotificationHandler(NotificationHandler handler);

    nlohmann::json initialize(
        std::string_view client_name,
        std::string_view client_version);

    nlohmann::json listTools();

    nlohmann::json callTool(
        std::string_view name,
        nlohmann::json arguments);

    void start();
    void stop();

private:
    void onMessage(std::string_view json);
    nlohmann::json sendRequest(
        std::string_view method,
        nlohmann::json params);

    std::unique_ptr<McpTransport> m_transport;
    NotificationHandler m_notification_handler;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    nlohmann::json m_pending_response;
    int m_pending_id = -1;
    int m_next_id = 1;
    bool m_initialized = false;
};

std::unique_ptr<McpClient> makeMcpClient(
    std::unique_ptr<McpTransport> transport);

} // namespace rook::adapters::mcp
