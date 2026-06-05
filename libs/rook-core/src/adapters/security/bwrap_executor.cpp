#include "rook/adapters/security/bwrap_executor.hpp"

#include <unistd.h>

namespace rook::adapters::security {

std::vector<std::string> buildBwrapArgs(
    const Capability& cap,
    const std::string& command,
    const std::vector<std::string>& args)
{
    std::vector<std::string> result = {"bwrap"};

    for (const char* dir : {"/usr", "/etc", "/lib", "/lib64", "/bin"}) {
        if (access(dir, F_OK) == 0) {
            result.push_back("--ro-bind");
            result.push_back(dir);
            result.push_back(dir);
        }
    }

    result.insert(result.end(), {"--dev", "/dev", "--proc", "/proc", "--tmpfs", "/tmp"});

    for (const auto& path : cap.readPaths()) {
        result.push_back("--ro-bind");
        result.push_back(path);
        result.push_back(path);
    }

    for (const auto& path : cap.writePaths()) {
        result.push_back("--bind");
        result.push_back(path);
        result.push_back(path);
    }

    if (!cap.allowsNetwork()) {
        result.push_back("--unshare-net");
    }

    result.push_back("--unshare-pid");
    result.push_back("--die-with-parent");

    result.push_back("--");
    result.push_back(command);
    for (const auto& arg : args) {
        result.push_back(arg);
    }

    return result;
}

} // namespace rook::adapters::security
