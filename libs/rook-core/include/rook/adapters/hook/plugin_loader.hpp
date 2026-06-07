#pragma once

#include "rook/adapters/hook/rook_plugin.h"
#include "rook/ports/hook_port.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rook::adapters::hook {

enum class VersionMatch {
    Compatible,
    NeedsRecompile,
    TooNew
};

VersionMatch checkPluginVersion(const RookPluginInfo& plugin,
                                int host_major,
                                int host_minor);

class PluginLoader {
public:
    std::vector<std::unique_ptr<ports::HookPort>> loadFromDirectory(
        std::string_view dir_path,
        const RookCoreAPI& core_api);

private:
    std::unique_ptr<ports::HookPort> loadSingle(
        const std::string& path,
        const RookCoreAPI& core_api);
};

} // namespace rook::adapters::hook
