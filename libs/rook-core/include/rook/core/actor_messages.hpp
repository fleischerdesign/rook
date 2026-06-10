#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <functional>
#include <memory>
#include "rook/ports/llm_port.hpp"
#include "rook/ports/tool_port.hpp"

namespace rook::domain {

struct ActorUserInput {
    std::string chat_id;
    std::string content;
    std::string model;
};

struct ActorLlmChunk {
    std::string chat_id;
    std::string content;
    bool is_final = false;
    bool is_reasoning = false;
};

struct ActorLlmDone {
    std::string chat_id;
    std::string model;
};

struct ActorLlmError {
    std::string chat_id;
    std::string message;
};

struct ActorToolCallRequested {
    std::string chat_id;
    std::string tool_name;
    std::string arguments;
    std::string call_id;
};

struct ActorToolResult {
    std::string chat_id;
    std::string call_id;
    std::string result;
    bool is_error = false;
};

struct ActorPermissionRequest {
    std::string request_uuid;
    std::string chat_id;
    std::string model;
    std::vector<ports::ToolCall> calls;
};

struct ActorPermissionDecision {
    std::string request_uuid;
    struct Result {
        std::string call_id;
        int decision = 2;
    };
    std::vector<Result> results;
};

struct ActorPermissionTimeout {
    std::string request_uuid;
};

struct ActorStartTitleGeneration {
    std::string chat_id;
    std::string model;
    std::string user_message;
    std::string assistant_message;
};

struct ActorTitleReady {
    std::string chat_id;
    std::string title;
};

struct ActorCreateChat {
    std::string title;
    std::string model;
};

struct ActorDeleteChat {
    std::string chat_id;
};

struct ActorSelectChat {
    std::string chat_id;
};

struct ActorTogglePin {
    std::string chat_id;
};

struct ActorRenameChat {
    std::string chat_id;
    std::string title;
};

struct ActorToggleSkill {
    std::string chat_id;
    std::string skill_id;
    bool active;
};

struct ActorCancelChat {
    std::string chat_id;
};

struct ActorDeleteExtension {
    std::string ext_name;
};

struct ActorVoiceToggle {
    bool enabled;
};

struct ActorVoiceMute {
    bool muted;
};

struct ActorWakeDetected {
    std::string keyword;
};

struct ActorWakeQuery {
    std::string transcript;
    std::string model;
    bool is_final = false;
};

struct ActorLiveUtterance {
    std::string chat_id;
    std::string transcript;
    std::string model;
    bool is_final = false;
};

struct ActorTtsFinished {};

struct ActorSttEmpty {};

struct ActorVoiceLiveToggle {
    std::string chat_id;
    bool enabled;
};

struct ActorBargeIn {};

using ActorMessage = std::variant<
    ActorUserInput,
    ActorLlmChunk,
    ActorLlmDone,
    ActorLlmError,
    ActorToolCallRequested,
    ActorToolResult,
    ActorPermissionRequest,
    ActorPermissionDecision,
    ActorPermissionTimeout,
    ActorStartTitleGeneration,
    ActorTitleReady,
    ActorCreateChat,
    ActorDeleteChat,
    ActorSelectChat,
    ActorTogglePin,
    ActorRenameChat,
    ActorToggleSkill,
    ActorCancelChat,
    ActorDeleteExtension,
    ActorVoiceToggle,
    ActorVoiceMute,
    ActorWakeDetected,
    ActorWakeQuery,
    ActorLiveUtterance,
    ActorTtsFinished,
    ActorSttEmpty,
    ActorVoiceLiveToggle,
    ActorBargeIn
>;

struct StateConversation {
    std::string id;
    std::string title;
    std::string model;
    std::vector<ports::LlmMessage> messages;
    std::vector<std::string> active_skill_ids;
    bool pinned = false;
    uint64_t pinned_at = 0;
    bool title_is_manual = false;
    bool has_messages = false;
};

struct StateSnapshot {
    std::vector<StateConversation> conversations;
    std::string active_chat_id;
    bool processing = false;
};

} // namespace rook::domain
