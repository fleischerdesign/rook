#pragma once

#include "rook/adapters/hook/rook_plugin.h"

#include <memory>
#include <string>

namespace rook::adapters::hook {

RookCoreAPI makeCoreAPI(std::string config_json = "{}");

} // namespace rook::adapters::hook
