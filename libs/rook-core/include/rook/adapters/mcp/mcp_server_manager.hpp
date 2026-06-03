#pragma once

#include "rook/adapters/mcp/mcp_client.hpp"
#include "rook/ports/tool_port.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rook::ports {
class SecurityPort;
}

namespace rook::adapters::mcp {

enum class McpTransportType {
    Stdio,
    HttpSse
};

struct McpServerConfig {
    std::string id;
    McpTransportType transport_type = McpTransportType::Stdio;
    std::string command;
    std::vector<std::string> args;
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    bool enabled = true;
    std::string source;
};

class McpServerManager {
public:
    void addServer(McpServerConfig config);

    void removeServer(std::string_view id);

    void startAll(std::string_view client_name = "rook",
                  std::string_view client_version = "0.1.0");

    void stopAll();

    std::vector<rook::ports::ToolDefinition> listAllTools();

    rook::ports::ToolResult executeTool(const rook::ports::ToolCall& call);

    size_t serverCount() const;

    std::vector<McpServerConfig> listServers() const;

    size_t toolCountForServer(std::string_view id) const;

    void setEnabled(std::string_view id, bool enabled);

    void setSecurityPort(rook::ports::SecurityPort* port);

private:
    struct ServerEntry {
        McpServerConfig config;
        std::unique_ptr<McpClient> client;
        std::vector<rook::ports::ToolDefinition> tools;
        bool started = false;
    };

    ServerEntry* findEntry(std::string_view id);
    ServerEntry* findToolOwner(std::string_view tool_name);

    std::vector<ServerEntry> m_servers;
    std::unordered_map<std::string, size_t> m_tool_index;
    mutable std::mutex m_mutex;
    rook::ports::SecurityPort* m_security = nullptr;
};

} // namespace rook::adapters::mcp
