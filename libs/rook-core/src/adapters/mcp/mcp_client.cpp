#include "rook/adapters/mcp/mcp_client.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace rook::adapters::mcp {

McpClient::McpClient(std::unique_ptr<McpTransport> transport)
    : m_transport(std::move(transport))
{
}

McpClient::~McpClient()
{
    stop();
}

void McpClient::setNotificationHandler(NotificationHandler handler)
{
    m_notification_handler = std::move(handler);
}

void McpClient::start()
{
    m_transport->setMessageHandler(
        [this](std::string_view json) { onMessage(json); });
    m_transport->start();
}

void McpClient::stop()
{
    m_transport->stop();
}

void McpClient::onMessage(std::string_view json)
{
    nlohmann::json msg;
    try {
        msg = nlohmann::json::parse(json);
    } catch (const nlohmann::json::parse_error& e) {
        spdlog::warn("McpClient: failed to parse message: {}", e.what());
        return;
    }

    if (msg.contains("id") && !msg.contains("method")) {
        int response_id = msg["id"].get<int>();
        std::lock_guard<std::mutex> lock(m_mutex);
        if (response_id == m_pending_id) {
            m_pending_response = std::move(msg);
            m_pending_id = -1;
            m_cv.notify_one();
        }
        return;
    }

    if (msg.contains("method") && !msg.contains("id")) {
        if (m_notification_handler) {
            auto method = msg["method"].get<std::string>();
            auto params = msg.value("params", nlohmann::json::object());
            m_notification_handler(method, std::move(params));
        }
        return;
    }

    spdlog::warn("McpClient: unexpected message format");
}

nlohmann::json McpClient::sendRequest(
    std::string_view method,
    nlohmann::json params)
{
    int id = m_next_id++;

    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", std::move(params)}
    };

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending_id = id;
        m_pending_response = nullptr;
    }

    m_transport->send(request.dump());

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        bool received = m_cv.wait_for(
            lock,
            std::chrono::seconds(30),
            [this] { return m_pending_response != nullptr; });

        if (!received) {
            m_pending_id = -1;
            throw std::runtime_error(
                "McpClient: request '" + std::string(method) + "' timed out");
        }

        auto response = std::move(m_pending_response);
        m_pending_response = nullptr;

        if (response.contains("error")) {
            auto& err = response["error"];
            throw std::runtime_error(
                "McpClient: " + std::string(method) + " error " +
                std::to_string(err.value("code", 0)) + ": " +
                err.value("message", "unknown"));
        }

        return response.value("result", nlohmann::json::object());
    }
}

nlohmann::json McpClient::initialize(
    std::string_view client_name,
    std::string_view client_version)
{
    if (m_initialized) {
        throw std::runtime_error("McpClient: already initialized");
    }

    nlohmann::json params = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"clientInfo", {
            {"name", client_name},
            {"version", client_version}
        }}
    };

    auto result = sendRequest("initialize", std::move(params));
    m_initialized = true;

    nlohmann::json notification = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}
    };
    m_transport->send(notification.dump());

    spdlog::info("McpClient: initialized with server {} {}",
        result.value("serverInfo", nlohmann::json::object())
            .value("name", "unknown"),
        result.value("serverInfo", nlohmann::json::object())
            .value("version", ""));

    return result;
}

nlohmann::json McpClient::listTools()
{
    if (!m_initialized) {
        throw std::runtime_error("McpClient: not initialized");
    }

    auto result = sendRequest("tools/list", nullptr);

    spdlog::info("McpClient: listed {} tools",
        result.value("tools", nlohmann::json::array()).size());

    return result;
}

nlohmann::json McpClient::callTool(
    std::string_view name,
    nlohmann::json arguments)
{
    if (!m_initialized) {
        throw std::runtime_error("McpClient: not initialized");
    }

    nlohmann::json params = {
        {"name", name},
        {"arguments", std::move(arguments)}
    };

    return sendRequest("tools/call", std::move(params));
}

std::unique_ptr<McpClient> makeMcpClient(
    std::unique_ptr<McpTransport> transport)
{
    return std::make_unique<McpClient>(std::move(transport));
}

} // namespace rook::adapters::mcp
