#pragma once

#include "rook/ports/hook_port.hpp"

#include <memory>

namespace rook::adapters::hook {

std::unique_ptr<ports::HookPort> makeSkillContextHook();

std::unique_ptr<ports::HookPort> makeResponseCleanupHook();

} // namespace rook::adapters::hook
