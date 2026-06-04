#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <cstdint>

namespace rook::domain {

struct UserInputReceived {
    std::string chat_id;
    std::string content;
    std::string source; // "text" | "voice"
    std::string model;
};

struct LlmStreamChunk {
    std::string chat_id;
    std::string content;
    bool is_final = false;
    bool is_reasoning = false;
};

struct LlmRequested {
    std::string chat_id;
    std::string model;
};

struct LlmCompleted {
    std::string chat_id;
    std::int32_t tokens_used = 0;
};

struct LlmError {
    std::string chat_id;
    std::string message;
};

struct ToolCallRequested {
    std::string chat_id;
    std::string tool_name;
    std::string arguments;
    std::string call_id;
};

struct ToolCallCompleted {
    std::string chat_id;
    std::string call_id;
    std::string result;
    bool is_error = false;
};

struct AudioWakeDetected {
    std::string wake_word;
};

struct AudioStateChanged {
    int old_state;
    int new_state;
};

struct SttResult {
    std::string transcript;
    bool is_final = false;
};

struct TtsStarted {
    std::string text;
};

struct TtsCompleted {};

struct ChatCreated {
    std::string chat_id;
};

struct ChatDeleted {
    std::string chat_id;
};

struct ChatSelected {
    std::string chat_id;
};

struct ChatUpdated {
    std::string chat_id;
    std::string title;
};

struct SettingsChanged {
    std::string key;
    std::string value;
};

struct SyncStateReceived {
    std::string node_id;
    std::vector<std::uint8_t> crdt_data;
};

struct TaskDelegated {
    std::string task_id;
    std::string client_id;
};

struct TaskCompleted {
    std::string task_id;
    std::string result;
};

struct SkillToggled {
    std::string chat_id;
    std::string skill_id;
    bool active;
};

struct ChatPinned {
    std::string chat_id;
    bool pinned;
};

struct ToolCallPermissionRequest {
    std::string request_uuid;
    std::string chat_id;
    struct CallInfo {
        std::string call_id;
        std::string tool_name;
        std::string arguments;
    };
    std::vector<CallInfo> calls;
};

struct ToolCallPermissionDecision {
    std::string request_uuid;
    struct Result {
        std::string call_id;
        int decision = 2; // 0=Allow, 1=Deny, 2=AllowAlways
    };
    std::vector<Result> results;
};

struct ToolCallTimedOut {
    std::string request_uuid;
};

using DomainEvent = std::variant<
    UserInputReceived,
    LlmStreamChunk,
    LlmRequested,
    LlmCompleted,
    LlmError,
    ToolCallRequested,
    ToolCallCompleted,
    AudioWakeDetected,
    AudioStateChanged,
    SttResult,
    TtsStarted,
    TtsCompleted,
    ChatCreated,
    ChatDeleted,
    ChatSelected,
    ChatUpdated,
    SettingsChanged,
    SyncStateReceived,
    TaskDelegated,
    TaskCompleted,
    SkillToggled,
    ChatPinned,
    ToolCallPermissionRequest,
    ToolCallPermissionDecision,
    ToolCallTimedOut
>;

} // namespace rook::domain
