#pragma once

#include "rook/adapters/mcp/mcp_transport.hpp"
#include "rook/adapters/security/capability.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace rook::adapters::mcp {

class StdioTransport final : public McpTransport {
public:
    StdioTransport(std::string command, std::vector<std::string> args);
    ~StdioTransport() override;

    void send(std::string_view json) override;
    void setMessageHandler(MessageHandler handler) override;
    void start() override;
    void stop() override;

    void setSandbox(std::optional<security::Capability> cap);

private:
    void readerLoop();
    void cleanupChild();

    std::string m_command;
    std::vector<std::string> m_args;
    MessageHandler m_handler;

    std::optional<security::Capability> m_sandbox;

    int m_stdin_fd = -1;
    int m_stdout_fd = -1;
    int m_child_pid = -1;
    std::atomic<bool> m_running{false};
    std::thread m_reader_thread;
};

} // namespace rook::adapters::mcp
