#pragma once

#include "rook/ports/llm_port.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace rook::ports {

enum class HookPoint {
    PreUserInput,
    PreLLM,
    PostLLM,
    PreToolExecution,
    PostToolExecution,
    PreResponse,
    OnSystemStartup,
    OnSystemShutdown
};

struct HookContext {
    HookPoint point;
    std::string chat_id;
    std::vector<LlmMessage>* messages = nullptr;
    std::string* response = nullptr;
    std::string* tool_args = nullptr;
    std::string* tool_result = nullptr;
    std::string* user_input = nullptr;
    std::string* system_prompt = nullptr;
};

class HookPort {
public:
    virtual ~HookPort() = default;

    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual std::vector<HookPoint> triggerPoints() const = 0;
    virtual int priority() const { return 0; }
    virtual void execute(HookContext& ctx) = 0;
};

} // namespace rook::ports
