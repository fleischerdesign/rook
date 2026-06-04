#pragma once

#include "rook/ports/tool_port.hpp"
#include "rook/ports/security_port.hpp"
#include <string>
#include <memory>

namespace rook::adapters::security {

class SecurityGuardedToolPort final : public ports::ToolPort {
public:
    SecurityGuardedToolPort(const ports::SecurityPort& security,
                            std::unique_ptr<ports::ToolPort> inner,
                            std::string server_id);

    std::vector<ports::ToolDefinition> listTools() override;

    ports::ToolResult execute(const ports::ToolCall& call) override;

private:
    const ports::SecurityPort& m_security;
    std::unique_ptr<ports::ToolPort> m_inner;
    std::string m_server_id;
};

} // namespace rook::adapters::security
