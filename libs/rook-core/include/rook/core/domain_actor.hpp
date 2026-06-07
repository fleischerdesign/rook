#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <stop_token>
#include <atomic>
#include <functional>
#include <vector>
#include <optional>
#include <unordered_map>
#include "rook/core/actor_messages.hpp"
#include "rook/domain/events.hpp"
#include "rook/adapters/hook/hook_registry.hpp"

namespace rook::ports {
class LlmPort;
class ToolPort;
class ToolPermissionPort;
class StorePort;
class ExtensionPort;
}

namespace rook::adapters::extension {
struct CustomSkill;
}

namespace rook::core {

template <typename T> class MpMcQueue;
class WorkerPool;

} // namespace rook::core

namespace rook::domain {

class ConversationManager;
using UiEventFn = std::function<void(DomainEvent)>;

} // namespace rook::domain

namespace rook::core {

class DomainActor {
public:
    DomainActor();
    ~DomainActor();

    DomainActor(const DomainActor&) = delete;
    DomainActor& operator=(const DomainActor&) = delete;

    void start(ports::LlmPort& llm,
               ports::ToolPort& tool,
               ports::ToolPermissionPort* permission_port,
               ports::StorePort* store,
               domain::UiEventFn on_ui_event,
               ports::ExtensionPort* extensions = nullptr,
               std::vector<rook::adapters::extension::CustomSkill>* custom_skills = nullptr);

    void stop();

    void post(domain::ActorMessage msg);

    bool is_running() const { return m_running.load(std::memory_order_acquire); }

    domain::ConversationManager& conv() { return *m_conv; }

    rook::adapters::hook::HookRegistry& hooks() { return m_hooks; }

private:
    void run(std::stop_token token);
    void dispatchMessage(const domain::ActorMessage& msg);

    void handleUserInput(const struct domain::ActorUserInput& msg);
    void handleLlmChunk(const struct domain::ActorLlmChunk& msg);
    void handleLlmDone(const struct domain::ActorLlmDone& msg);
    void handleLlmError(const struct domain::ActorLlmError& msg);
    void handleToolCallRequested(const struct domain::ActorToolCallRequested& msg);
    void handleToolResult(const struct domain::ActorToolResult& msg);
    void handlePermissionRequest(const struct domain::ActorPermissionRequest& msg);
    void handlePermissionDecision(const struct domain::ActorPermissionDecision& msg);
    void handlePermissionTimeout(const struct domain::ActorPermissionTimeout& msg);
    void handleTitleReady(const struct domain::ActorTitleReady& msg);
    void handleCreateChat(const struct domain::ActorCreateChat& msg);
    void handleDeleteChat(const struct domain::ActorDeleteChat& msg);
    void handleSelectChat(const struct domain::ActorSelectChat& msg);
    void handleTogglePin(const struct domain::ActorTogglePin& msg);
    void handleRenameChat(const struct domain::ActorRenameChat& msg);
    void handleToggleSkill(const struct domain::ActorToggleSkill& msg);

    void runLlm(std::string chat_id, std::string model);
    void processTools(std::string chat_id, std::string model);
    void executeTool(const std::string& chat_id, const ports::ToolCall& call);
    void resumeAfterBatch(std::string chat_id);
    std::string buildToolsJson();
    std::string buildSystemPrompt(std::string_view chat_id);
    void injectSkillsOnFirstMessage(std::string_view chat_id);
    void rebuildSystemMessage(std::string_view chat_id);
    void saveActiveConv();
    void emitUiEvent(domain::DomainEvent event);
    void emitSnapshot();
    void syncAlwaysOnSkills(std::string_view chat_id);

    std::unique_ptr<domain::ConversationManager> m_conv;
    ports::LlmPort* m_llm = nullptr;
    ports::ToolPort* m_tool_port = nullptr;
    ports::ToolPermissionPort* m_perm_port = nullptr;
    ports::StorePort* m_store = nullptr;
    ports::ExtensionPort* m_extensions = nullptr;
    std::vector<rook::adapters::extension::CustomSkill>* m_custom_skills = nullptr;

    domain::UiEventFn m_ui_event_fn;

    std::unique_ptr<WorkerPool> m_pool;

    using Queue = MpMcQueue<domain::ActorMessage>;
    std::unique_ptr<Queue> m_inbox;

    std::jthread m_thread;
    std::atomic<bool> m_running{false};

    std::vector<ports::ToolCall> m_pending_tool_calls;

    struct PendingPerm {
        std::string chat_id;
        std::string model;
        std::vector<ports::ToolCall> calls;
    };
    std::unordered_map<std::string, PendingPerm> m_pending_perms;

    struct ActiveBatch {
        std::string model;
        int remaining = 0;
        bool finalized = false;
    };
    std::optional<ActiveBatch> m_active_batch;

    rook::adapters::hook::HookRegistry m_hooks;
};

} // namespace rook::core
