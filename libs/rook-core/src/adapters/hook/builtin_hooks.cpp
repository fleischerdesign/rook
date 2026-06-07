#include "rook/adapters/hook/builtin_hooks.hpp"
#include "rook/ports/extension_port.hpp"
#include "rook/adapters/extension/extension_manifest.hpp"

#include <spdlog/spdlog.h>

namespace rook::adapters::hook {

namespace {

class ExtensionContextHook final : public ports::HookPort {
public:
    ExtensionContextHook(ports::ExtensionPort* extensions,
                         std::vector<extension::CustomSkill>* custom_skills)
        : m_extensions(extensions)
        , m_custom_skills(custom_skills)
    {}

    std::string id() const override { return "builtin.extension_context"; }
    std::string name() const override { return "Extension Context"; }

    std::vector<ports::HookPoint> triggerPoints() const override
    {
        return {ports::HookPoint::PreLLM};
    }

    int priority() const override { return 100; }

    void execute(ports::HookContext& ctx) override
    {
        if (!m_extensions || !ctx.messages) return;

        std::string context_index;

        for (auto& ext : m_extensions->listInstalled()) {
            if (ext.context_files.empty()) continue;

            context_index += "\nFrom " + ext.display_name
                          + " v" + ext.version + ":\n";
            for (auto& cf : ext.context_files) {
                context_index += "  " + ext.install_path + "/" + cf.path;
                if (!cf.description.empty())
                    context_index += " — " + cf.description;
                context_index += "\n";
            }
        }

        if (context_index.empty()) return;

        ports::LlmMessage sys_msg;
        sys_msg.role = "system";
        sys_msg.content = "Available extension context files "
                         "(use read_file to access):"
                         + context_index;

        ctx.messages->insert(ctx.messages->begin(), std::move(sys_msg));
    }

private:
    ports::ExtensionPort* m_extensions;
    std::vector<extension::CustomSkill>* m_custom_skills;
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

        while (!s.empty()
               && (s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
            s.pop_back();
    }
};

} // namespace

std::unique_ptr<ports::HookPort> makeExtensionContextHook(
    ports::ExtensionPort* extensions,
    std::vector<extension::CustomSkill>* custom_skills)
{
    return std::make_unique<ExtensionContextHook>(
        extensions, custom_skills);
}

std::unique_ptr<ports::HookPort> makeResponseCleanupHook()
{
    return std::make_unique<ResponseCleanupHook>();
}

} // namespace rook::adapters::hook
