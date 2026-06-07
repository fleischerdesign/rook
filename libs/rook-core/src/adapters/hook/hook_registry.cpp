#include "rook/adapters/hook/hook_registry.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace rook::adapters::hook {

void HookRegistry::registerHook(std::unique_ptr<ports::HookPort> hook)
{
    m_hooks.push_back(std::move(hook));
}

void HookRegistry::trigger(ports::HookPoint point, ports::HookContext& ctx)
{
    std::vector<ports::HookPort*> matching;
    for (const auto& h : m_hooks) {
        if (!h->isActive()) continue;
        const auto pts = h->triggerPoints();
        if (std::find(pts.begin(), pts.end(), point) != pts.end()) {
            matching.push_back(h.get());
        }
    }

    std::sort(matching.begin(), matching.end(),
              [](const ports::HookPort* a, const ports::HookPort* b) {
                  return a->priority() < b->priority();
              });

    for (auto* h : matching) {
        try {
            h->execute(ctx);
        } catch (const std::exception& e) {
            spdlog::error("Hook '{}' failed at point {}: {}",
                          h->id(), static_cast<int>(point), e.what());
        }
    }
}

bool HookRegistry::contains(std::string_view id) const
{
    for (const auto& h : m_hooks) {
        if (h->id() == id) return true;
    }
    return false;
}

void HookRegistry::deactivateByPrefix(std::string_view prefix)
{
    int count = 0;
    for (auto& h : m_hooks) {
        if (h->isActive() && h->id().starts_with(prefix)) {
            h->deactivate();
            count++;
        }
    }
    if (count > 0)
        spdlog::info("HookRegistry: deactivated {} hook(s) with prefix '{}'",
                     count, prefix);
}

} // namespace rook::adapters::hook
