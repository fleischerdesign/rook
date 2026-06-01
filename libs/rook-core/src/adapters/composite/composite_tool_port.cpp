#include "rook/adapters/composite/composite_tool_port.hpp"

#include <spdlog/spdlog.h>

namespace rook::adapters::composite {

void CompositeToolPort::addPort(std::unique_ptr<rook::ports::ToolPort> port)
{
    m_ports.push_back(std::move(port));
    rebuildRouting();
}

std::vector<rook::ports::ToolDefinition> CompositeToolPort::listTools()
{
    std::vector<rook::ports::ToolDefinition> all;
    for (auto& port : m_ports) {
        auto tools = port->listTools();
        all.insert(all.end(), tools.begin(), tools.end());
    }
    return all;
}

rook::ports::ToolResult CompositeToolPort::execute(const rook::ports::ToolCall& call)
{
    auto it = m_routing.find(call.name);
    if (it != m_routing.end() && it->second < m_ports.size()) {
        return m_ports[it->second]->execute(call);
    }

    rook::ports::ToolResult result;
    result.id = call.id;
    result.is_error = true;
    result.content = "unknown tool: " + call.name;
    return result;
}

void CompositeToolPort::rebuildRouting()
{
    m_routing.clear();
    for (size_t i = 0; i < m_ports.size(); ++i) {
        auto tools = m_ports[i]->listTools();
        for (auto& t : tools) {
            m_routing[t.name] = i;
        }
    }
}

} // namespace rook::adapters::composite
