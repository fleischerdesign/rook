#include "rook/adapters/hook/builtin_hooks.hpp"

#include <functional>
#include <spdlog/spdlog.h>

namespace rook::adapters::hook {

namespace {

class GenericPreLlmHook final : public ports::HookPort {
public:
    GenericPreLlmHook(std::string hook_id, std::string hook_name,
                      std::function<void(ports::HookContext&)> fn)
        : m_id(std::move(hook_id))
        , m_name(std::move(hook_name))
        , m_fn(std::move(fn))
    {}

    std::string id() const override { return m_id; }
    std::string name() const override { return m_name; }

    std::vector<ports::HookPoint> triggerPoints() const override
    {
        return {ports::HookPoint::PreLLM};
    }

    int priority() const override { return 100; }

    void execute(ports::HookContext& ctx) override
    {
        if (m_fn) m_fn(ctx);
    }

private:
    std::string m_id;
    std::string m_name;
    std::function<void(ports::HookContext&)> m_fn;
};

class ResponseCleanupHook final : public ports::HookPort {
public:
    std::string id() const override { return "builtin.response_cleanup"; }
    std::string name() const override { return "Response Cleanup"; }

    std::vector<ports::HookPoint> triggerPoints() const override
    {
        return {ports::HookPoint::PreResponse};
    }

    int priority() const override { return 200; }

    void execute(ports::HookContext& ctx) override
    {
        if (!ctx.response) return;

        auto& s = *ctx.response;

        while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
            s.pop_back();
    }
};

} // namespace

std::unique_ptr<ports::HookPort> makeSkillContextHook()
{
    return std::make_unique<GenericPreLlmHook>(
        "builtin.skill_context",
        "Skill Context",
        [](ports::HookContext&) {
            spdlog::debug("SkillContextHook: no extensions configured");
        });
}

std::unique_ptr<ports::HookPort> makeResponseCleanupHook()
{
    return std::make_unique<ResponseCleanupHook>();
}

} // namespace rook::adapters::hook
