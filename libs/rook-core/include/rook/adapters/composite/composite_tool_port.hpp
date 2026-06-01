#pragma once

#include "rook/ports/tool_port.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rook::adapters::composite {

class CompositeToolPort : public rook::ports::ToolPort {
public:
    void addPort(std::unique_ptr<rook::ports::ToolPort> port);

    std::vector<rook::ports::ToolDefinition> listTools() override;

    rook::ports::ToolResult execute(const rook::ports::ToolCall& call) override;

private:
    void rebuildRouting();

    std::vector<std::unique_ptr<rook::ports::ToolPort>> m_ports;
    std::unordered_map<std::string, size_t> m_routing;
};

} // namespace rook::adapters::composite
