#include "rook/adapters/security/security_guarded_tool_port.hpp"
#include <spdlog/spdlog.h>

namespace rook::adapters::security {

SecurityGuardedToolPort::SecurityGuardedToolPort(
    const ports::SecurityPort& security,
    ports::ToolPort& inner,
    std::string server_id)
    : m_security(security)
    , m_inner(inner)
    , m_server_id(std::move(server_id))
{}

std::vector<ports::ToolDefinition> SecurityGuardedToolPort::listTools()
{
    return m_inner.listTools();
}

ports::ToolResult SecurityGuardedToolPort::execute(
    const ports::ToolCall& call)
{
    if (!m_security.isAllowed(m_server_id, call)) {
        spdlog::warn("Security: denied tool {} on server {}",
                     call.name, m_server_id);
        return ports::ToolResult{
            .id = call.id,
            .content = std::string("Permission denied: tool '")
                     + call.name + "' not allowed on server '"
                     + m_server_id + "'",
            .is_error = true,
        };
    }

    return m_inner.execute(call);
}

} // namespace rook::adapters::security
