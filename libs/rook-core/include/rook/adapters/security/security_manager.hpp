#pragma once

#include "rook/ports/security_port.hpp"
#include "rook/adapters/security/capability.hpp"
#include <map>
#include <string>
#include <string_view>

namespace rook::adapters::security {

class SecurityManager final : public ports::SecurityPort {
public:
    SecurityManager() = default;

    bool isAllowed(std::string_view server_id,
                   const ports::ToolCall& call) const override;

    void loadFromConfig(std::string_view config_json) override;

    const Capability* findCapability(std::string_view server_id) const;

private:
    std::map<std::string, Capability, std::less<>> m_capabilities;
};

} // namespace rook::adapters::security
