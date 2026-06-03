#pragma once

#include "rook/ports/extension_port.hpp"
#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace rook::adapters::extension {

class ExtensionManager final : public ports::ExtensionPort {
public:
    explicit ExtensionManager(std::string data_dir);

    ExtensionManager(std::string data_dir,
                     mcp::McpServerManager* mcp);

    std::vector<InstalledExtension> listInstalled() override;

    bool install(std::string_view github_url) override;

    bool uninstall(std::string_view name) override;

    bool update(std::string_view name) override;

    void setMcpManager(mcp::McpServerManager* mcp) { m_mcp = mcp; }

    void setSkillEnabled(std::string_view ext_name,
                         std::string_view skill_name, bool enabled);
    void setServerEnabled(std::string_view ext_name,
                          std::string_view server_id, bool enabled);

    void loadFromConfig(std::string_view json);
    std::string saveToConfig() const;

private:
    std::string cloneRepo(std::string_view url);
    std::string readFile(std::string_view path);
    bool removeDir(std::string_view path);
    std::string extractRepoName(std::string_view url);

    void registerMcpServers(const InstalledExtension& ext);
    void unregisterMcpServers(const InstalledExtension& ext);

    std::string m_data_dir;
    mcp::McpServerManager* m_mcp = nullptr;
    std::vector<InstalledExtension> m_installed;
    mutable std::mutex m_mutex;
};

} // namespace rook::adapters::extension
