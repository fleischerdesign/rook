#pragma once

#include "rook/ports/tool_port.hpp"
#include "rook/adapters/mcp/mcp_server_manager.hpp"

#include <memory>

namespace rook::adapters::mcp {

class McpClientAdapter : public rook::ports::ToolPort {
public:
    explicit McpClientAdapter(McpServerManager& manager);

    std::vector<rook::ports::ToolDefinition> listTools() override;

    rook::ports::ToolResult execute(const rook::ports::ToolCall& call) override;

private:
    McpServerManager& m_manager;
};

std::unique_ptr<rook::ports::ToolPort> makeMcpToolPort(
    McpServerManager& manager);

} // namespace rook::adapters::mcp
