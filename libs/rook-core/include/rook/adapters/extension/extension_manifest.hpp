#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <chrono>
#include "rook/adapters/mcp/mcp_server_manager.hpp"

namespace rook::adapters::extension {

struct ExtensionMcpServer {
    std::string id;
    std::string command;
    std::vector<std::string> args;
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    bool enabled = true;
};

struct ExtensionSkill {
    std::string name;
    std::string description;
    std::string prompt;
    bool enabled = true;
};

struct ExtensionCommand {
    std::string name;
    std::string description;
    std::string prompt;
};

struct ExtensionContextFile {
    std::string path;
    std::string description;
};

struct CustomSkill {
    std::string name;
    std::string description;
    std::string prompt;
    bool enabled = false;
};

struct ExtensionManifest {
    std::string name;
    std::string display_name;
    std::string version;
    std::string description;
    std::string author;
    std::string license;
    std::string homepage;

    std::vector<ExtensionMcpServer> mcp_servers;
    std::vector<ExtensionSkill> skills;
    std::vector<ExtensionCommand> commands;
    std::vector<ExtensionContextFile> context_files;

    bool valid = false;
    std::string error;
};

struct InstalledExtension {
    std::string name;
    std::string display_name;
    std::string version;
    std::string description;
    std::string author;
    std::string url;
    std::string install_path;
    std::chrono::system_clock::time_point installed_at;

    std::vector<ExtensionMcpServer> mcp_servers;
    std::vector<ExtensionSkill> skills;
    std::vector<ExtensionCommand> commands;
    std::vector<ExtensionContextFile> context_files;
};

ExtensionManifest parseManifest(std::string_view json);

} // namespace rook::adapters::extension
