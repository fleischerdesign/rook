#pragma once

#include "rook/adapters/security/capability.hpp"

#include <string>
#include <vector>

namespace rook::adapters::security {

std::vector<std::string> buildBwrapArgs(
    const Capability& cap,
    const std::string& command,
    const std::vector<std::string>& args);

} // namespace rook::adapters::security
