#pragma once

#include "rook/ports/tool_port.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace rook::adapters::builtin {

class BuiltinToolPort : public rook::ports::ToolPort {
public:
    BuiltinToolPort();

    std::vector<rook::ports::ToolDefinition> listTools() override;

    rook::ports::ToolResult execute(const rook::ports::ToolCall& call) override;

private:
    rook::ports::ToolResult readFile(nlohmann::json args);
    rook::ports::ToolResult writeFile(nlohmann::json args);
    rook::ports::ToolResult listDirectory(nlohmann::json args);
};

std::unique_ptr<rook::ports::ToolPort> makeBuiltinToolPort();

} // namespace rook::adapters::builtin
