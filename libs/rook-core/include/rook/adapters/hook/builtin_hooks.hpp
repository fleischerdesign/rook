#pragma once

#include "rook/ports/hook_port.hpp"

#include <memory>
#include <vector>

namespace rook::ports {
class ExtensionPort;
}

namespace rook::adapters::extension {
struct CustomSkill;
}

namespace rook::adapters::hook {

std::unique_ptr<ports::HookPort> makeExtensionContextHook(
    ports::ExtensionPort* extensions,
    std::vector<extension::CustomSkill>* custom_skills);

std::unique_ptr<ports::HookPort> makeResponseCleanupHook();

} // namespace rook::adapters::hook
