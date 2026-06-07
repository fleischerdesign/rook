#include "rook/adapters/extension/extension_manager.hpp"
#include "rook/adapters/extension/extension_manifest.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>

namespace rook::adapters::extension {

ExtensionManager::ExtensionManager(std::string data_dir)
    : m_data_dir(std::move(data_dir))
{
}

ExtensionManager::ExtensionManager(std::string data_dir,
                                   mcp::McpServerManager* mcp)
    : m_data_dir(std::move(data_dir))
    , m_mcp(mcp)
{
}

std::vector<InstalledExtension> ExtensionManager::listInstalled()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_installed;
}

bool ExtensionManager::install(std::string_view github_url)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto repo_name = extractRepoName(github_url);
    if (repo_name.empty()) {
        spdlog::error("ExtensionManager: could not extract repo name from {}",
                      github_url);
        return false;
    }

    for (auto& ext : m_installed) {
        if (ext.name == repo_name) {
            spdlog::warn("ExtensionManager: {} already installed", repo_name);
            return false;
        }
    }

    auto install_path = cloneRepo(github_url);
    if (install_path.empty()) {
        return false;
    }

    auto manifest_path = install_path + "/rook-extension.json";
    auto manifest_json = readFile(manifest_path);

    auto manifest = parseManifest(manifest_json);
    if (!manifest.valid) {
        spdlog::error("ExtensionManager: invalid manifest in {}", repo_name);
        removeDir(install_path);
        return false;
    }

    InstalledExtension ext;
    ext.name = manifest.name.empty() ? repo_name : manifest.name;
    ext.display_name = manifest.display_name.empty()
                       ? ext.name : manifest.display_name;
    ext.version = manifest.version;
    ext.description = manifest.description;
    ext.author = manifest.author;
    ext.url = std::string(github_url);
    ext.install_path = install_path;
    ext.installed_at = std::chrono::system_clock::now();
    ext.mcp_servers = manifest.mcp_servers;
    ext.skills = manifest.skills;
    ext.commands = manifest.commands;
    ext.context_files = manifest.context_files;
    ext.plugin_paths = manifest.plugin_paths;

    registerMcpServers(ext);

    m_installed.push_back(std::move(ext));

    spdlog::info("ExtensionManager: installed {}", repo_name);
    return true;
}

bool ExtensionManager::uninstall(std::string_view name)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = std::find_if(m_installed.begin(), m_installed.end(),
        [name](auto& e) { return e.name == name; });

    if (it == m_installed.end()) {
        spdlog::warn("ExtensionManager: {} not installed", name);
        return false;
    }

    unregisterMcpServers(*it);

    if (!it->install_path.empty()) {
        removeDir(it->install_path);
    }

    m_installed.erase(it);
    spdlog::info("ExtensionManager: uninstalled {}", name);
    return true;
}

bool ExtensionManager::update(std::string_view name)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = std::find_if(m_installed.begin(), m_installed.end(),
        [name](auto& e) { return e.name == name; });

    if (it == m_installed.end()) return false;

    std::set<std::string> disabled_ids;
    for (auto& s : it->mcp_servers) {
        if (!s.enabled) disabled_ids.insert(s.id);
    }

    auto url = it->url;

    unregisterMcpServers(*it);
    m_installed.erase(it);

    if (!install(url)) {
        return false;
    }

    for (auto& ext : m_installed) {
        if (ext.name == name) {
            for (auto& s : ext.mcp_servers) {
                if (disabled_ids.count(s.id)) s.enabled = false;
            }
            break;
        }
    }

    spdlog::info("ExtensionManager: updated {}", name);
    return true;
}

void ExtensionManager::setSkillEnabled(std::string_view ext_name,
                                        std::string_view skill_name,
                                        bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& ext : m_installed) {
        if (ext.name == ext_name) {
            for (auto& s : ext.skills) {
                if (s.name == skill_name) { s.enabled = enabled; return; }
            }
        }
    }
}

void ExtensionManager::setServerEnabled(std::string_view ext_name,
                                         std::string_view server_id,
                                         bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& ext : m_installed) {
        if (ext.name == ext_name) {
            for (auto& s : ext.mcp_servers) {
                if (s.id == server_id) { s.enabled = enabled; return; }
            }
        }
    }
}

void ExtensionManager::loadFromConfig(std::string_view json)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (json.empty()) return;

    try {
        auto j = nlohmann::json::parse(json);
        if (!j.is_array()) return;

        m_installed.clear();
        for (auto& entry : j) {
            InstalledExtension ext;
            ext.name = entry.value("name", "");
            ext.display_name = entry.value("display_name", ext.name);
            ext.version = entry.value("version", "");
            ext.description = entry.value("description", "");
            ext.author = entry.value("author", "");
            ext.url = entry.value("url", "");
            ext.install_path = entry.value("install_path", "");

            auto ts = entry.value("installed_at", 0);
            ext.installed_at = std::chrono::system_clock::from_time_t(
                static_cast<std::time_t>(ts));

            if (!ext.name.empty() && !ext.url.empty() && !ext.install_path.empty()) {
                auto manifest_path = ext.install_path + "/rook-extension.json";
                auto manifest_json = readFile(manifest_path);
                auto manifest = parseManifest(manifest_json);

                if (manifest.valid) {
                    ext.mcp_servers = manifest.mcp_servers;
                    ext.skills = manifest.skills;
                    ext.commands = manifest.commands;
                    ext.context_files = manifest.context_files;
                    ext.plugin_paths = manifest.plugin_paths;
                }

                if (entry.contains("disabled_servers") &&
                    entry["disabled_servers"].is_object()) {
                    for (auto& [sid, val] : entry["disabled_servers"].items()) {
                        if (val.is_boolean() && !val.get<bool>()) {
                            for (auto& s : ext.mcp_servers) {
                                if (s.id == sid) { s.enabled = false; break; }
                            }
                        }
                    }
                }

                if (entry.contains("always_on_skills") &&
                    entry["always_on_skills"].is_object()) {
                    for (auto& [sn, val] : entry["always_on_skills"].items()) {
                        if (val.is_boolean() && val.get<bool>()) {
                            for (auto& s : ext.skills) {
                                if (s.name == sn) { s.enabled = true; break; }
                            }
                        }
                    }
                }

                registerMcpServers(ext);
                m_installed.push_back(std::move(ext));
            }
        }

        spdlog::info("ExtensionManager: loaded {} installed extensions",
                     m_installed.size());
    } catch (const std::exception& e) {
        spdlog::error("ExtensionManager: failed to load config: {}", e.what());
    }
}

std::string ExtensionManager::saveToConfig() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    nlohmann::json arr = nlohmann::json::array();
    for (auto& ext : m_installed) {
        nlohmann::json entry;
        entry["name"] = ext.name;
        entry["display_name"] = ext.display_name;
        entry["version"] = ext.version;
        entry["description"] = ext.description;
        entry["author"] = ext.author;
        entry["url"] = ext.url;
        entry["install_path"] = ext.install_path;
        entry["installed_at"] = std::chrono::duration_cast<
            std::chrono::seconds>(ext.installed_at.time_since_epoch()).count();

        if (m_mcp) {
            nlohmann::json states = nlohmann::json::object();
            auto servers = m_mcp->listServers();
            for (auto& srv : servers) {
                std::string prefix = "ext:" + ext.name + ":";
                if (srv.source.starts_with("extension:" + ext.name) &&
                    srv.id.starts_with(prefix) && !srv.enabled) {
                    auto local_id = srv.id.substr(prefix.size());
                    states[local_id] = false;
                }
            }
            if (!states.empty()) entry["disabled_servers"] = states;

            nlohmann::json skill_states = nlohmann::json::object();
            for (auto& s : ext.skills) {
                if (s.enabled) skill_states[s.name] = true;
            }
            if (!skill_states.empty()) entry["always_on_skills"] = skill_states;
        }

        arr.push_back(entry);
    }
    return arr.dump(2);
}

std::string ExtensionManager::cloneRepo(std::string_view url)
{
    auto extensions_dir = m_data_dir + "/extensions";
    std::filesystem::create_directories(extensions_dir);

    auto repo_name = extractRepoName(url);
    auto target = extensions_dir + "/" + repo_name;

    struct stat st;
    if (::stat(target.c_str(), &st) == 0) {
        std::filesystem::remove_all(target);
    }

    std::string url_str(url);
    bool is_local = url_str.starts_with("file://") || url_str.starts_with("/");

    if (is_local) {
        auto local_path = url_str.starts_with("file://")
            ? url_str.substr(7) : url_str;

        if (local_path.ends_with("/"))
            local_path.pop_back();

        std::error_code ec;
        std::filesystem::copy(local_path, target,
            std::filesystem::copy_options::recursive, ec);
        if (ec) {
            spdlog::error("ExtensionManager: local copy failed for {}: {}",
                         local_path, ec.message());
            return "";
        }

        spdlog::info("ExtensionManager: copied {} to {}", local_path, target);
        return target;
    }

    std::string cmd = "git clone --depth 1 ";
    cmd += std::string(url) + ".git ";
    cmd += target + " 2>&1";

    auto* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) {
        spdlog::error("ExtensionManager: git clone failed for {}", url);
        return "";
    }

    std::string output;
    char buf[256];
    while (::fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }

    int rc = ::pclose(pipe);
    if (rc != 0) {
        spdlog::error("ExtensionManager: git clone failed (rc={}): {}",
                      rc, output);
        std::filesystem::remove_all(target);
        return "";
    }

    spdlog::info("ExtensionManager: cloned {} to {}", url, target);
    return target;
}

std::string ExtensionManager::readFile(std::string_view path)
{
    std::ifstream f{std::string(path)};
    if (!f.is_open()) return "";

    std::stringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

bool ExtensionManager::removeDir(std::string_view path)
{
    std::error_code ec;
    std::filesystem::remove_all(std::string(path), ec);
    if (ec) {
        spdlog::warn("ExtensionManager: could not remove {}: {}",
                     path, ec.message());
        return false;
    }
    return true;
}

std::string ExtensionManager::extractRepoName(std::string_view url)
{
    auto pos = url.rfind('/');
    if (pos == std::string_view::npos) return std::string(url);

    auto name = std::string(url.substr(pos + 1));

    if (name.ends_with(".git"))
        name = name.substr(0, name.size() - 4);

    return name;
}

void ExtensionManager::registerMcpServers(const InstalledExtension& ext)
{
    if (!m_mcp) return;

    for (auto& s : ext.mcp_servers) {
        mcp::McpServerConfig cfg;
        cfg.source = "extension:" + ext.name;
        cfg.enabled = s.enabled;

        if (!s.url.empty()) {
            cfg.transport_type = mcp::McpTransportType::HttpSse;
            cfg.id = "ext:" + ext.name + ":" + s.id;
            cfg.url = s.url;
            cfg.headers = s.headers;
        } else {
            cfg.transport_type = mcp::McpTransportType::Stdio;
            cfg.id = "ext:" + ext.name + ":" + s.id;
            cfg.command = s.command;
            cfg.args = s.args;
        }

        try {
            m_mcp->addServer(std::move(cfg));
            spdlog::info("ExtensionManager: registered MCP server {} for {}",
                         s.id, ext.name);
        } catch (const std::exception& e) {
            spdlog::warn("ExtensionManager: could not register MCP server {}: {}",
                         s.id, e.what());
        }
    }
}

void ExtensionManager::unregisterMcpServers(const InstalledExtension& ext)
{
    if (!m_mcp) return;

    for (auto& s : ext.mcp_servers) {
        m_mcp->removeServer("ext:" + ext.name + ":" + s.id);
    }
}

} // namespace rook::adapters::extension
