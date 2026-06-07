#pragma once

#include "rook/ports/hook_port.hpp"
#include "rook/adapters/hook/rook_plugin.h"

#include <string>
#include <string_view>
#include <vector>

namespace rook::adapters::hook {

struct PluginFnTable {
    const int* (*get_trigger_points)(void* instance);
    int (*get_priority)(void* instance);
    void (*execute)(void* instance, RookHookContext* ctx);
    void (*destroy)(void* instance);
};

class PluginHookAdapter final : public ports::HookPort {
public:
    PluginHookAdapter(void* plugin_instance,
                      RookPluginInfo info,
                      PluginFnTable fns,
                      void* dl_handle);

    ~PluginHookAdapter() override;

    PluginHookAdapter(const PluginHookAdapter&) = delete;
    PluginHookAdapter& operator=(const PluginHookAdapter&) = delete;

    std::string id() const override;
    std::string name() const override;
    std::vector<ports::HookPoint> triggerPoints() const override;
    int priority() const override;
    void execute(ports::HookContext& ctx) override;

private:
    static ports::HookPoint fromCHookPoint(int c_hook_point);
    static int toCHookPoint(ports::HookPoint point);

    void serializeContext(const ports::HookContext& ctx,
                          std::string& json_out) const;
    void deserializeOutput(const std::string& changed,
                           ports::HookContext& ctx) const;

    void* m_instance;
    RookPluginInfo m_info;
    PluginFnTable m_fns;
    void* m_dl_handle;
    std::vector<ports::HookPoint> m_trigger_points;
    int m_priority;
};

} // namespace rook::adapters::hook
