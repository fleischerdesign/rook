#include "rook/adapters/mcp/null_tool_port.hpp"

namespace rook::adapters::mcp {

namespace {

class NullToolPort final : public rook::ports::ToolPort {
public:
    std::vector<rook::ports::ToolDefinition> listTools() override {
        return {};
    }

    rook::ports::ToolResult execute(const rook::ports::ToolCall& call) override {
        rook::ports::ToolResult result;
        result.id = call.id;
        result.is_error = true;
        result.content = "no tool adapter configured";
        return result;
    }
};

} // namespace

std::unique_ptr<rook::ports::ToolPort> makeNullToolPort() {
    return std::make_unique<NullToolPort>();
}

} // namespace rook::adapters::mcp
