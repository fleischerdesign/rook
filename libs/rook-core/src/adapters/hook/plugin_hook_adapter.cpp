#include "rook/adapters/hook/plugin_hook_adapter.hpp"

#include <dlfcn.h>
#include <nlohmann/json.hpp>

#include <cstring>

namespace rook::adapters::hook {

PluginHookAdapter::PluginHookAdapter(void* plugin_instance,
                                     RookPluginInfo info,
                                     PluginFnTable fns,
                                     void* dl_handle)
    : m_instance(plugin_instance)
    , m_info(std::move(info))
    , m_fns(fns)
    , m_dl_handle(dl_handle)
{
    if (m_fns.get_trigger_points) {
        const int* pts = m_fns.get_trigger_points(m_instance);
        if (pts) {
            for (int i = 0; pts[i] != -1; ++i)
                m_trigger_points.push_back(fromCHookPoint(pts[i]));
        }
    }

    if (m_fns.get_priority) {
        m_priority = m_fns.get_priority(m_instance);
    }
}

PluginHookAdapter::~PluginHookAdapter()
{
    if (m_instance && m_fns.destroy) {
        m_fns.destroy(m_instance);
        m_instance = nullptr;
    }
    if (m_dl_handle) {
        dlclose(m_dl_handle);
        m_dl_handle = nullptr;
    }
}

std::string PluginHookAdapter::id() const
{
    return m_info.id ? std::string(m_info.id) : std::string{};
}

std::string PluginHookAdapter::name() const
{
    return m_info.name ? std::string(m_info.name) : std::string{};
}

std::vector<ports::HookPoint> PluginHookAdapter::triggerPoints() const
{
    return m_trigger_points;
}

int PluginHookAdapter::priority() const
{
    return m_priority;
}

void PluginHookAdapter::execute(ports::HookContext& ctx)
{
    if (!m_instance || !m_fns.execute) return;

    std::string input_json;
    serializeContext(ctx, input_json);

    std::string output_buffer;
    output_buffer.resize(65536, '\0');

    RookHookContext c_ctx{};
    c_ctx.hook_point = toCHookPoint(ctx.point);
    c_ctx.chat_id = ctx.chat_id.c_str();
    c_ctx.input = input_json.c_str();
    c_ctx.output = output_buffer.data();
    c_ctx.output_capacity = static_cast<int>(output_buffer.size());

    m_fns.execute(m_instance, &c_ctx);

    output_buffer.resize(std::strlen(output_buffer.c_str()));

    if (!output_buffer.empty()) {
        deserializeOutput(output_buffer, ctx);
    }
}

ports::HookPoint PluginHookAdapter::fromCHookPoint(int c)
{
    switch (c) {
    case ROOK_HOOK_PRE_USER_INPUT:       return ports::HookPoint::PreUserInput;
    case ROOK_HOOK_PRE_LLM:              return ports::HookPoint::PreLLM;
    case ROOK_HOOK_POST_LLM:             return ports::HookPoint::PostLLM;
    case ROOK_HOOK_PRE_TOOL_EXECUTION:   return ports::HookPoint::PreToolExecution;
    case ROOK_HOOK_POST_TOOL_EXECUTION:  return ports::HookPoint::PostToolExecution;
    case ROOK_HOOK_PRE_RESPONSE:         return ports::HookPoint::PreResponse;
    case ROOK_HOOK_ON_SYSTEM_STARTUP:    return ports::HookPoint::OnSystemStartup;
    case ROOK_HOOK_ON_SYSTEM_SHUTDOWN:   return ports::HookPoint::OnSystemShutdown;
    default: return ports::HookPoint::PreResponse;
    }
}

int PluginHookAdapter::toCHookPoint(ports::HookPoint point)
{
    switch (point) {
    case ports::HookPoint::PreUserInput:      return ROOK_HOOK_PRE_USER_INPUT;
    case ports::HookPoint::PreLLM:            return ROOK_HOOK_PRE_LLM;
    case ports::HookPoint::PostLLM:           return ROOK_HOOK_POST_LLM;
    case ports::HookPoint::PreToolExecution:  return ROOK_HOOK_PRE_TOOL_EXECUTION;
    case ports::HookPoint::PostToolExecution: return ROOK_HOOK_POST_TOOL_EXECUTION;
    case ports::HookPoint::PreResponse:       return ROOK_HOOK_PRE_RESPONSE;
    case ports::HookPoint::OnSystemStartup:   return ROOK_HOOK_ON_SYSTEM_STARTUP;
    case ports::HookPoint::OnSystemShutdown:  return ROOK_HOOK_ON_SYSTEM_SHUTDOWN;
    }
    return ROOK_HOOK_PRE_RESPONSE;
}

void PluginHookAdapter::serializeContext(const ports::HookContext& ctx,
                                         std::string& json_out) const
{
    nlohmann::json j;
    j["hook_point"] = toCHookPoint(ctx.point);
    j["chat_id"] = ctx.chat_id;

    switch (ctx.point) {
    case ports::HookPoint::PreLLM:
        if (ctx.messages) {
            auto arr = nlohmann::json::array();
            for (const auto& msg : *ctx.messages) {
                nlohmann::json m;
                m["role"] = msg.role;
                m["content"] = msg.content;
                if (!msg.tool_call_id.empty())
                    m["tool_call_id"] = msg.tool_call_id;
                if (!msg.tool_calls.empty())
                    m["tool_calls"] = msg.tool_calls;
                arr.push_back(std::move(m));
            }
            j["messages"] = std::move(arr);
        }
        break;

    case ports::HookPoint::PostLLM:
    case ports::HookPoint::PreResponse:
        if (ctx.response)
            j["response"] = *ctx.response;
        break;

    case ports::HookPoint::PreToolExecution:
        if (ctx.tool_args)
            j["tool_args"] = *ctx.tool_args;
        break;

    case ports::HookPoint::PostToolExecution:
        if (ctx.tool_result)
            j["tool_result"] = *ctx.tool_result;
        break;

    case ports::HookPoint::PreUserInput:
        if (ctx.user_input)
            j["user_input"] = *ctx.user_input;
        break;

    default:
        break;
    }

    json_out = j.dump();
}

void PluginHookAdapter::deserializeOutput(const std::string& changed,
                                          ports::HookContext& ctx) const
{
    nlohmann::json j = nlohmann::json::parse(changed, nullptr, false);
    if (j.is_discarded()) return;

    switch (ctx.point) {
    case ports::HookPoint::PreUserInput:
        if (j.contains("user_input") && j["user_input"].is_string() && ctx.user_input)
            *ctx.user_input = j["user_input"].get<std::string>();
        break;

    case ports::HookPoint::PreLLM:
        if (j.contains("messages") && j["messages"].is_array() && ctx.messages) {
            ctx.messages->clear();
            for (const auto& m : j["messages"]) {
                ports::LlmMessage msg;
                msg.role = m.value("role", "");
                msg.content = m.value("content", "");
                msg.tool_call_id = m.value("tool_call_id", "");
                msg.tool_calls = m.value("tool_calls", "");
                msg.tool_name = m.value("tool_name", "");
                ctx.messages->push_back(std::move(msg));
            }
        }
        break;

    case ports::HookPoint::PostLLM:
    case ports::HookPoint::PreResponse:
        if (j.contains("response") && j["response"].is_string() && ctx.response)
            *ctx.response = j["response"].get<std::string>();
        break;

    case ports::HookPoint::PreToolExecution:
        if (j.contains("tool_args") && j["tool_args"].is_string() && ctx.tool_args)
            *ctx.tool_args = j["tool_args"].get<std::string>();
        break;

    case ports::HookPoint::PostToolExecution:
        if (j.contains("tool_result") && j["tool_result"].is_string() && ctx.tool_result)
            *ctx.tool_result = j["tool_result"].get<std::string>();
        break;

    default:
        break;
    }
}

} // namespace rook::adapters::hook
