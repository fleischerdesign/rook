#pragma once

#include "rook/adapters/mcp/mcp_transport.hpp"
#include "rook/adapters/llm/llm_http_client.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace rook::adapters::mcp {

class HttpSseTransport final : public McpTransport {
public:
    HttpSseTransport(std::string url,
                     std::vector<std::pair<std::string, std::string>> headers);
    ~HttpSseTransport() override;

    void send(std::string_view json) override;
    void setMessageHandler(MessageHandler handler) override;
    void start() override;
    void stop() override;

private:
    static std::string resolveEnvVars(std::string_view value);

    std::string m_url;
    std::vector<std::pair<std::string, std::string>> m_headers;
    std::unique_ptr<llm::LlmHttpClient> m_http;
    MessageHandler m_handler;
    std::atomic<bool> m_running{false};
    std::thread m_listener_thread;
    mutable std::mutex m_mutex;
};

} // namespace rook::adapters::mcp
