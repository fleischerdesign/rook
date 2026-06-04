#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <functional>

namespace rook::ports {

struct ToolParameter {
    std::string name;
    std::string type;
    std::string description;
    bool required = false;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<ToolParameter> parameters;
    std::string source;
};

struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments;
};

struct ToolResult {
    std::string id;
    std::string content;
    bool is_error = false;
};

class ToolPort {
public:
    virtual ~ToolPort() = default;

    virtual std::vector<ToolDefinition> listTools() = 0;

    virtual ToolResult execute(const ToolCall& call) = 0;
};

class ToolPermissionPort {
public:
    virtual ~ToolPermissionPort() = default;

    enum class Decision {
        Allow,
        Deny,
        AllowAlways,
    };

    virtual bool isWhitelisted(std::string_view tool_name) const = 0;
    virtual void addToWhitelist(std::string_view tool_name) = 0;
};

} // namespace rook::ports
