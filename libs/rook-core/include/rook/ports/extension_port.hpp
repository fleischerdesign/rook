#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "rook/adapters/extension/extension_manifest.hpp"

namespace rook::ports {

class ExtensionPort {
public:
    virtual ~ExtensionPort() = default;

    virtual std::vector<rook::adapters::extension::InstalledExtension>
    listInstalled() = 0;

    virtual bool install(std::string_view github_url) = 0;

    virtual bool uninstall(std::string_view name) = 0;

    virtual bool update(std::string_view name) = 0;
};

} // namespace rook::ports
