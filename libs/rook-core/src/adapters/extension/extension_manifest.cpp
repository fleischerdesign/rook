#include "rook/adapters/extension/extension_manifest.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace rook::adapters::extension {

ExtensionManifest parseManifest(std::string_view json)
{
    ExtensionManifest m;

    if (json.empty()) {
        m.error = "empty manifest";
        return m;
    }

    try {
        auto j = nlohmann::json::parse(json);

        m.name = j.value("name", "");
        m.display_name = j.value("display_name", m.name);
        m.version = j.value("version", "0.1.0");
        m.description = j.value("description", "");
        m.author = j.value("author", "");
        m.license = j.value("license", "");
        m.homepage = j.value("homepage", "");

        if (m.name.empty()) {
            m.error = "manifest missing 'name'";
            return m;
        }

        if (j.contains("mcp_servers") && j["mcp_servers"].is_array()) {
            for (auto& s : j["mcp_servers"]) {
                ExtensionMcpServer server;
                server.id = s.value("id", "");
                server.command = s.value("command", "");
                server.url = s.value("url", "");
                server.enabled = s.value("enabled", true);

                if (s.contains("args") && s["args"].is_array()) {
                    for (auto& a : s["args"]) {
                        if (a.is_string())
                            server.args.push_back(a.get<std::string>());
                    }
                }

                if (s.contains("headers") && s["headers"].is_object()) {
                    for (auto& [k, v] : s["headers"].items()) {
                        if (v.is_string())
                            server.headers.emplace_back(k, v.get<std::string>());
                    }
                }

                bool has_stdio = !server.id.empty() && !server.command.empty();
                bool has_http = !server.id.empty() && !server.url.empty();

                if (has_stdio || has_http) {
                    m.mcp_servers.push_back(std::move(server));
                }
            }
        }

        if (j.contains("skills") && j["skills"].is_array()) {
            for (auto& s : j["skills"]) {
                ExtensionSkill skill;
                skill.name = s.value("name", "");
                skill.description = s.value("description", "");
                skill.prompt = s.value("prompt", "");

                if (!skill.name.empty()) {
                    m.skills.push_back(std::move(skill));
                }
            }
        }

        if (j.contains("commands") && j["commands"].is_array()) {
            for (auto& c : j["commands"]) {
                ExtensionCommand cmd;
                cmd.name = c.value("name", "");
                cmd.description = c.value("description", "");
                cmd.prompt = c.value("prompt", "");

                if (!cmd.name.empty()) {
                    m.commands.push_back(std::move(cmd));
                }
            }
        }

        if (j.contains("context_files") && j["context_files"].is_array()) {
            for (auto& c : j["context_files"]) {
                ExtensionContextFile cf;
                cf.path = c.value("path", "");
                cf.description = c.value("description", "");

                if (!cf.path.empty()) {
                    m.context_files.push_back(std::move(cf));
                }
            }
        }

        m.valid = true;
    } catch (const std::exception& e) {
        m.error = std::string("failed to parse manifest: ") + e.what();
        spdlog::error("ExtensionManifest: {}", m.error);
    }

    return m;
}

} // namespace rook::adapters::extension
