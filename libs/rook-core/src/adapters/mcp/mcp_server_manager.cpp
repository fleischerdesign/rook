#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include "rook/adapters/mcp/stdio_transport.hpp"
#include "rook/ports/security_port.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace rook::adapters::mcp {

void McpServerManager::addServer(McpServerConfig config)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& entry : m_servers) {
        if (entry.config.id == config.id) {
            throw std::runtime_error(
                "McpServerManager: server '" + config.id + "' already exists");
        }
    }

    ServerEntry entry;
    entry.config = std::move(config);
    m_servers.push_back(std::move(entry));

    spdlog::info("McpServerManager: added server '{}'", m_servers.back().config.id);
}

void McpServerManager::removeServer(std::string_view id)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_servers.begin();
    for (; it != m_servers.end(); ++it) {
        if (it->config.id == id) break;
    }

    if (it == m_servers.end()) return;

    if (it->started) {
        it->client->stop();
    }

    for (auto& tool : it->tools) {
        m_tool_index.erase(tool.name);
    }

    m_servers.erase(it);

    {
        size_t idx = 0;
        for (auto& entry : m_servers) {
            for (auto& tool : entry.tools) {
                m_tool_index[tool.name] = idx;
            }
            ++idx;
        }
    }

    spdlog::info("McpServerManager: removed server '{}'", id);
}

void McpServerManager::startAll(
    std::string_view client_name,
    std::string_view client_version)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (size_t i = 0; i < m_servers.size(); ++i) {
        auto& entry = m_servers[i];
        if (entry.started) continue;
        if (!entry.config.enabled) continue;

        try {
            auto transport = makeStdioTransport(
                entry.config.command, entry.config.args);

            auto client = makeMcpClient(std::move(transport));
            client->start();
            client->initialize(client_name, client_version);

            auto result = client->listTools();
            auto& tools_json = result["tools"];
            if (tools_json.is_array()) {
                for (auto& t : tools_json) {
                    rook::ports::ToolDefinition def;
                    def.name = t["name"].get<std::string>();
                    def.description = t.value("description", "");

                    if (t.contains("inputSchema") &&
                        t["inputSchema"].contains("properties")) {
                        auto& props = t["inputSchema"]["properties"];
                        auto& required = t["inputSchema"]["required"];
                        for (auto& [pname, pschema] : props.items()) {
                            rook::ports::ToolParameter param;
                            param.name = pname;
                            param.type = pschema.value("type", "string");
                            param.description = pschema.value("description", "");

                            if (required.is_array()) {
                                for (auto& r : required) {
                                    if (r.get<std::string>() == pname) {
                                        param.required = true;
                                        break;
                                    }
                                }
                            }

                            def.parameters.push_back(std::move(param));
                        }
                    }

                    entry.tools.push_back(std::move(def));
                    m_tool_index[entry.tools.back().name] = i;
                }
            }

            entry.client = std::move(client);
            entry.started = true;

            spdlog::info("McpServerManager: started '{}' with {} tools",
                entry.config.id, entry.tools.size());

        } catch (const std::exception& e) {
            spdlog::error("McpServerManager: failed to start '{}': {}",
                entry.config.id, e.what());
        }
    }
}

void McpServerManager::stopAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& entry : m_servers) {
        if (!entry.started) continue;
        try {
            entry.client->stop();
        } catch (const std::exception& e) {
            spdlog::warn("McpServerManager: error stopping '{}': {}",
                entry.config.id, e.what());
        }
        entry.started = false;
    }

    spdlog::info("McpServerManager: all servers stopped");
}

std::vector<rook::ports::ToolDefinition> McpServerManager::listAllTools()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<rook::ports::ToolDefinition> all;
    for (auto& entry : m_servers) {
        if (!entry.started) continue;
        for (auto& tool : entry.tools) {
            all.push_back(tool);
        }
    }
    return all;
}

rook::ports::ToolResult McpServerManager::executeTool(
    const rook::ports::ToolCall& call)
{
    ServerEntry* owner = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        owner = findToolOwner(call.name);
    }

    rook::ports::ToolResult result;
    result.id = call.id;

    if (!owner || !owner->started) {
        result.is_error = true;
        result.content = "tool not found: " + call.name;
        return result;
    }

    if (m_security && !m_security->isAllowed(owner->config.id, call)) {
        result.is_error = true;
        result.content = std::string("permission denied: tool '")
                       + call.name + "' not allowed on server '"
                       + owner->config.id + "'";
        return result;
    }

    try {
        auto args = nlohmann::json::parse(call.arguments);
        auto response = owner->client->callTool(call.name, std::move(args));

        if (response.contains("content")) {
            auto& content = response["content"];
            if (content.is_array() && !content.empty()) {
                auto& first = content[0];
                if (first.contains("text")) {
                    result.content = first["text"].get<std::string>();
                } else {
                    result.content = response.dump();
                }
            } else {
                result.content = response.dump();
            }
        } else {
            result.content = response.dump();
        }
    } catch (const std::exception& e) {
        result.is_error = true;
        result.content = std::string("tool execution failed: ") + e.what();
        spdlog::error("McpServerManager: tool '{}' failed: {}", call.name, e.what());
    }

    return result;
}

size_t McpServerManager::serverCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_servers.size();
}

void McpServerManager::setSecurityPort(rook::ports::SecurityPort* port)
{
    m_security = port;
}

McpServerManager::ServerEntry*
McpServerManager::findEntry(std::string_view id)
{
    for (auto& entry : m_servers) {
        if (entry.config.id == id) return &entry;
    }
    return nullptr;
}

McpServerManager::ServerEntry*
McpServerManager::findToolOwner(std::string_view tool_name)
{
    auto it = m_tool_index.find(std::string(tool_name));
    if (it == m_tool_index.end()) return nullptr;
    size_t idx = it->second;
    if (idx >= m_servers.size()) return nullptr;
    return &m_servers[idx];
}

} // namespace rook::adapters::mcp
