#pragma once

#include "rook/ports/hook_port.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace rook::adapters::hook {

class HookRegistry {
public:
    void registerHook(std::unique_ptr<ports::HookPort> hook);
    void trigger(ports::HookPoint point, ports::HookContext& ctx);
    bool contains(std::string_view id) const;
    void deactivateByPrefix(std::string_view prefix);

private:
    std::vector<std::unique_ptr<ports::HookPort>> m_hooks;
};

} // namespace rook::adapters::hook
