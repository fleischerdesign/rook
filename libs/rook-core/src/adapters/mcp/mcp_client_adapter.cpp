#include "rook/adapters/mcp/mcp_client_adapter.hpp"

namespace rook::adapters::mcp {

McpClientAdapter::McpClientAdapter(McpServerManager& manager)
    : m_manager(manager)
{
}

std::vector<rook::ports::ToolDefinition> McpClientAdapter::listTools()
{
    return m_manager.listAllTools();
}

rook::ports::ToolResult McpClientAdapter::execute(
    const rook::ports::ToolCall& call)
{
    return m_manager.executeTool(call);
}

std::unique_ptr<rook::ports::ToolPort> makeMcpToolPort(
    McpServerManager& manager)
{
    return std::make_unique<McpClientAdapter>(manager);
}

} // namespace rook::adapters::mcp
