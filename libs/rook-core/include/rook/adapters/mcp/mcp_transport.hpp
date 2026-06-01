#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rook::adapters::mcp {

class McpTransport {
public:
    using MessageHandler = std::function<void(std::string_view json)>;

    virtual ~McpTransport() = default;

    virtual void send(std::string_view json) = 0;

    virtual void setMessageHandler(MessageHandler handler) = 0;

    virtual void start() = 0;

    virtual void stop() = 0;
};

std::unique_ptr<McpTransport> makeStdioTransport(
    std::string command,
    std::vector<std::string> args);

} // namespace rook::adapters::mcp
