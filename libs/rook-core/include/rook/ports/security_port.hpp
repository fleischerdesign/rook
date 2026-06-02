#pragma once

#include "rook/ports/tool_port.hpp"
#include <string>
#include <string_view>

namespace rook::ports {

class SecurityPort {
public:
    virtual ~SecurityPort() = default;

    virtual bool isAllowed(std::string_view server_id,
                           const ToolCall& call) const = 0;

    virtual void loadFromConfig(std::string_view config_json) = 0;
};

} // namespace rook::ports
