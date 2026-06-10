#include "rook/core/domain_actor.hpp"
#include "rook/core/actor_messages.hpp"
#include "rook/core/lockfree_queue.hpp"
#include "rook/core/worker_pool.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/domain/events.hpp"
#include "rook/ports/hook_port.hpp"
#include "rook/ports/llm_port.hpp"
#include "rook/ports/tool_port.hpp"
#include "rook/ports/store_port.hpp"
#include "rook/ports/extension_port.hpp"
#include "rook/ports/wakeword_port.hpp"
#include "rook/ports/speech_to_text_port.hpp"
#include "rook/ports/text_to_speech_port.hpp"
#include "rook/ports/audio_device_port.hpp"
#include "rook/adapters/extension/extension_manifest.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

namespace rook::core {

DomainActor::DomainActor()
    : m_conv(std::make_unique<domain::ConversationManager>())
    , m_pool(std::make_unique<WorkerPool>(4))
    , m_inbox(std::make_unique<Queue>(4096))
{}

DomainActor::~DomainActor() {
    stop();
    m_pool.reset();
}

void DomainActor::start(ports::LlmPort& llm,
                        ports::ToolPort& tool,
                        ports::ToolPermissionPort* permission_port,
                        ports::StorePort* store,
                        domain::UiEventFn on_ui_event,
                        ports::ExtensionPort* extensions,
                        std::vector<rook::adapters::extension::CustomSkill>* custom_skills)
{
    m_llm = &llm;
    m_tool_port = &tool;
    m_perm_port = permission_port;
    m_store = store;
    m_ui_event_fn = std::move(on_ui_event);
    m_extensions = extensions;
    m_custom_skills = custom_skills;

    m_conv->setStore(store);
    m_conv->loadFromStore(*store);

    m_running.store(true, std::memory_order_release);
    m_thread = std::jthread([this](std::stop_token token) { run(token); });

    spdlog::info("DomainActor started");
    emitSnapshot();

    {
        ports::HookContext ctx;
        ctx.point = ports::HookPoint::OnSystemStartup;
        m_hooks.trigger(ports::HookPoint::OnSystemStartup, ctx);
    }
}

void DomainActor::stop() {
    {
        ports::HookContext ctx;
        ctx.point = ports::HookPoint::OnSystemShutdown;
        m_hooks.trigger(ports::HookPoint::OnSystemShutdown, ctx);
    }

    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
    m_pool->stop();
}

void DomainActor::post(domain::ActorMessage msg) {
    if (!m_inbox->push(std::move(msg))) {
        spdlog::warn("DomainActor: inbox full, dropping message");
    }
}

void DomainActor::run(std::stop_token token) {
    while (!token.stop_requested()) {
        auto msg = m_inbox->pop();
        if (!msg) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        dispatchMessage(*msg);
    }

    while (auto msg = m_inbox->pop())
        dispatchMessage(*msg);
}

void DomainActor::dispatchMessage(const domain::ActorMessage& msg) {
    std::visit([this](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, domain::ActorUserInput>)
            handleUserInput(m);
        else if constexpr (std::is_same_v<T, domain::ActorLlmChunk>)
            handleLlmChunk(m);
        else if constexpr (std::is_same_v<T, domain::ActorLlmDone>)
            handleLlmDone(m);
        else if constexpr (std::is_same_v<T, domain::ActorLlmError>)
            handleLlmError(m);
        else if constexpr (std::is_same_v<T, domain::ActorToolCallRequested>)
            handleToolCallRequested(m);
        else if constexpr (std::is_same_v<T, domain::ActorToolResult>)
            handleToolResult(m);
        else if constexpr (std::is_same_v<T, domain::ActorPermissionRequest>)
            handlePermissionRequest(m);
        else if constexpr (std::is_same_v<T, domain::ActorPermissionDecision>)
            handlePermissionDecision(m);
        else if constexpr (std::is_same_v<T, domain::ActorPermissionTimeout>)
            handlePermissionTimeout(m);
        else if constexpr (std::is_same_v<T, domain::ActorTitleReady>)
            handleTitleReady(m);
        else if constexpr (std::is_same_v<T, domain::ActorCreateChat>)
            handleCreateChat(m);
        else if constexpr (std::is_same_v<T, domain::ActorDeleteChat>)
            handleDeleteChat(m);
        else if constexpr (std::is_same_v<T, domain::ActorSelectChat>)
            handleSelectChat(m);
        else if constexpr (std::is_same_v<T, domain::ActorTogglePin>)
            handleTogglePin(m);
        else if constexpr (std::is_same_v<T, domain::ActorRenameChat>)
            handleRenameChat(m);
        else if constexpr (std::is_same_v<T, domain::ActorToggleSkill>)
            handleToggleSkill(m);
        else if constexpr (std::is_same_v<T, domain::ActorVoiceToggle>)
            handleVoiceToggle(m);
        else if constexpr (std::is_same_v<T, domain::ActorVoiceMute>)
            handleVoiceMute(m);
        else if constexpr (std::is_same_v<T, domain::ActorWakeDetected>)
            handleWakeDetected(m);
        else if constexpr (std::is_same_v<T, domain::ActorWakeQuery>)
            handleWakeQuery(m);
        else if constexpr (std::is_same_v<T, domain::ActorLiveUtterance>)
            handleLiveUtterance(m);
        else if constexpr (std::is_same_v<T, domain::ActorTtsFinished>)
            handleTtsFinished(m);
        else if constexpr (std::is_same_v<T, domain::ActorSttEmpty>)
            handleSttEmpty(m);
        else if constexpr (std::is_same_v<T, domain::ActorVoiceLiveToggle>)
            handleVoiceLiveToggle(m);
        else if constexpr (std::is_same_v<T, domain::ActorBargeIn>)
            handleBargeIn(m);
    }, msg);
}

void DomainActor::emitUiEvent(domain::DomainEvent event) {
    if (m_ui_event_fn)
        m_ui_event_fn(std::move(event));
}

void DomainActor::emitSnapshot() {
    domain::SnapshotReady snap;
    for (auto& c : m_conv->list()) {
        domain::SnapshotConversation sc;
        sc.id = c.id;
        sc.title = c.title;
        sc.model = c.model;
        sc.pinned = c.pinned;
        sc.pinned_at = c.pinned_at;
        sc.has_messages = !c.messages.empty();
        sc.active_skill_ids = c.active_skill_ids;
        sc.updated_at = std::chrono::duration_cast<std::chrono::milliseconds>(
            c.updated_at.time_since_epoch()).count();
        sc.messages = m_conv->buildLlmMessages(c.id);
        snap.conversations.push_back(std::move(sc));
    }
    auto active = m_conv->active();
    if (active) snap.active_chat_id = active->id;
    emitUiEvent(std::move(snap));
}

void DomainActor::handleUserInput(const domain::ActorUserInput& msg) {
    spdlog::info("Actor: UserInput chat={}", msg.chat_id);

    std::string input = msg.content;
    {
        ports::HookContext ctx;
        ctx.point = ports::HookPoint::PreUserInput;
        ctx.chat_id = msg.chat_id;
        ctx.user_input = &input;
        m_hooks.trigger(ports::HookPoint::PreUserInput, ctx);
    }

    domain::ChatMessage cmsg;
    cmsg.role = "user";
    cmsg.content = std::move(input);
    m_conv->setModel(msg.chat_id, msg.model);

    auto conv = m_conv->open(msg.chat_id);
    bool is_first = conv.messages.empty();

    m_conv->addMessage(msg.chat_id, std::move(cmsg));
    saveActiveConv();
    emitSnapshot();

    if (is_first) {
        injectSkillsOnFirstMessage(msg.chat_id);
    }

    m_pending_tool_calls.clear();

    emitUiEvent(domain::LlmRequested{msg.chat_id, ""});
    runLlm(msg.chat_id, msg.model);
}

std::string DomainActor::buildToolsJson() {
    auto tools = m_tool_port->listTools();
    nlohmann::json arr = nlohmann::json::array();
    for (auto& t : tools) {
        nlohmann::json tool;
        tool["type"] = "function";
        tool["function"]["name"] = t.name;
        tool["function"]["description"] = t.description;

        nlohmann::json props = nlohmann::json::object();
        nlohmann::json required = nlohmann::json::array();
        for (auto& p : t.parameters) {
            nlohmann::json prop;
            prop["type"] = p.type;
            prop["description"] = p.description;
            props[p.name] = prop;
            if (p.required) required.push_back(p.name);
        }
        tool["function"]["parameters"]["type"] = "object";
        tool["function"]["parameters"]["properties"] = props;
        if (!required.empty())
            tool["function"]["parameters"]["required"] = required;
        arr.push_back(std::move(tool));
    }
    return arr.dump();
}

void DomainActor::runLlm(std::string chat_id, std::string model) {
    spdlog::info("Actor: runLlm chat={} model={}", chat_id, model);

    auto messages = m_conv->buildLlmMessages(chat_id);
    auto tools_json = buildToolsJson();

    {
        ports::HookContext ctx;
        ctx.point = ports::HookPoint::PreLLM;
        ctx.chat_id = chat_id;
        ctx.messages = &messages;
        m_hooks.trigger(ports::HookPoint::PreLLM, ctx);
    }

    auto* inbox = m_inbox.get();
    auto completed = std::make_shared<bool>(false);

    m_llm->streamChat(
        chat_id,
        messages,
        [inbox, chat_id, model, completed](
            std::string_view chunk, bool is_final, bool is_reasoning) {
            if (!chunk.empty()) {
                inbox->push(domain::ActorLlmChunk{
                    .chat_id = chat_id,
                    .content = std::string(chunk),
                    .is_final = is_final,
                    .is_reasoning = is_reasoning,
                });
            }
            if (is_final && !is_reasoning && !*completed) {
                *completed = true;
                inbox->push(domain::ActorLlmDone{
                    .chat_id = chat_id,
                    .model = model,
                });
            }
        },
        model,
        [inbox, chat_id](std::string_view name, std::string_view arguments,
                         std::string_view call_id) {
            inbox->push(domain::ActorToolCallRequested{
                .chat_id = chat_id,
                .tool_name = std::string(name),
                .arguments = std::string(arguments),
                .call_id = std::string(call_id),
            });
        },
        tools_json
    );
}

void DomainActor::handleLlmChunk(const domain::ActorLlmChunk& msg) {
    if (msg.chat_id.starts_with("__title__")) return;

    if (msg.is_reasoning) {
        m_conv->updateReasoningChunk(msg.chat_id, msg.content);
    } else {
        m_conv->updateAssistantChunk(msg.chat_id, msg.content);
    }

    emitUiEvent(domain::LlmStreamChunk{
        .chat_id = msg.chat_id,
        .content = msg.content,
        .is_final = msg.is_final,
        .is_reasoning = msg.is_reasoning,
    });
}

void DomainActor::handleLlmDone(const domain::ActorLlmDone& msg) {
    spdlog::info("Actor: LlmDone chat={}", msg.chat_id);

    emitUiEvent(domain::LlmCompleted{msg.chat_id, 0});

    {
        auto* response = m_conv->lastResponse(msg.chat_id);
        if (response) {
            ports::HookContext ctx;
            ctx.point = ports::HookPoint::PostLLM;
            ctx.chat_id = msg.chat_id;
            ctx.response = response;
            m_hooks.trigger(ports::HookPoint::PostLLM, ctx);
        }
    }
    {
        auto* response = m_conv->lastResponse(msg.chat_id);
        if (response) {
            ports::HookContext ctx;
            ctx.point = ports::HookPoint::PreResponse;
            ctx.chat_id = msg.chat_id;
            ctx.response = response;
            m_hooks.trigger(ports::HookPoint::PreResponse, ctx);
        }
    }

    if (m_pending_tool_calls.empty()) {
        auto* last = m_conv->lastResponse(msg.chat_id);
        bool ephemeral = m_conv->open(msg.chat_id).ephemeral;

        if (m_audio_pipeline && last) {
            auto it = m_voice_triggered_chats.find(msg.chat_id);
            if (it != m_voice_triggered_chats.end()) {
                emitUiEvent(domain::TtsStarted{*last});
                m_audio_pipeline->onResponseReady(*last);
                m_voice_triggered_chats.erase(it);

                if (ephemeral) {
                    m_conv->remove(msg.chat_id);
                    return;
                }
            }
        }

        if (!ephemeral) {
            m_conv->saveActiveConversation();
        }

        auto conv = m_conv->open(msg.chat_id);
        if (conv.title == "New Chat") {
            int user_count = 0;
            int assistant_count = 0;
            std::string last_user, last_assistant;

            for (const auto& cmsg : conv.messages) {
                if (cmsg.role == "user") { user_count++; last_user = cmsg.content; }
                if (cmsg.role == "assistant") { assistant_count++; last_assistant = cmsg.content; }
            }

            if (user_count == 1 && assistant_count == 1) {
                m_pool->submit(
                    [this, chat_id = msg.chat_id, model = conv.model,
                     last_user, last_assistant]() {
                        auto title_id = "__title__" + chat_id;

                        ports::LlmMessage t_sys;
                        t_sys.role = "system";
                        t_sys.content = "You are a title generator. Reply with ONLY the raw title text, "
                                        "no quotes, no prefixes, no markdown, no explanation.";

                        ports::LlmMessage t_usr;
                        t_usr.role = "user";
                        t_usr.content = "Generate a short title (max 5 words) in the same language "
                                        "for this conversation:\n\nUser: " + last_user +
                                        "\nAssistant: " + last_assistant;

                        auto* inbox = m_inbox.get();
                        auto accumulated = std::make_shared<std::string>();

                        m_llm->streamChat(
                            title_id,
                            {t_sys, t_usr},
                            [inbox, chat_id, accumulated](std::string_view chunk,
                                                          bool is_final, bool is_reasoning) {
                                if (is_reasoning) return;
                                *accumulated += std::string(chunk);
                                if (is_final && !accumulated->empty()) {
                                    auto& s = *accumulated;
                                    s.erase(0, s.find_first_not_of(" \t\n\r\"'"));
                                    s.erase(s.find_last_not_of(" \t\n\r\"'") + 1);
                                    for (auto p : {"Title:", "title:", "Title :", "title :",
                                                   "**", "##", "#"}) {
                                        if (s.starts_with(p)) { s.erase(0, std::strlen(p)); break; }
                                    }
                                    if (!s.empty() && s.back() == '.') s.pop_back();
                                    if (!s.empty()) {
                                        inbox->push(domain::ActorTitleReady{
                                            .chat_id = chat_id,
                                            .title = std::move(s),
                                        });
                                    }
                                }
                            },
                            model
                        );
                    });
        }
    }
    } else {
        processTools(msg.chat_id, msg.model);
    }
}

void DomainActor::handleLlmError(const domain::ActorLlmError& msg) {
    spdlog::error("Actor: LlmError chat={} {}", msg.chat_id, msg.message);
    emitUiEvent(domain::LlmError{msg.chat_id, msg.message});
}

void DomainActor::handleToolCallRequested(const domain::ActorToolCallRequested& msg) {
    ports::ToolCall call;
    call.id = msg.call_id;
    call.name = msg.tool_name;
    call.arguments = msg.arguments;
    m_pending_tool_calls.push_back(std::move(call));

    emitUiEvent(domain::ToolCallRequested{
        .chat_id = msg.chat_id,
        .tool_name = msg.tool_name,
        .arguments = msg.arguments,
        .call_id = msg.call_id,
    });
}

void DomainActor::processTools(std::string chat_id, std::string model) {
    auto calls = std::move(m_pending_tool_calls);
    m_pending_tool_calls.clear();

    auto conv = m_conv->open(chat_id);

    nlohmann::json tc_json = nlohmann::json::array();
    for (auto& tc : calls) {
        nlohmann::json item;
        item["id"] = tc.id;
        item["type"] = "function";
        item["function"]["name"] = tc.name;
        item["function"]["arguments"] = tc.arguments;
        tc_json.push_back(std::move(item));
    }
    m_conv->setAssistantToolCalls(chat_id, tc_json.dump());

    std::vector<ports::ToolCall> needs_permission;
    int whitelisted_count = 0;
    for (auto& call : calls) {
        if (m_perm_port && m_perm_port->isWhitelisted(chat_id, call.name)) {
            spdlog::info("Actor: tool {} whitelisted, executing", call.name);
            {
                ports::HookContext ctx;
                ctx.point = ports::HookPoint::PreToolExecution;
                ctx.chat_id = chat_id;
                ctx.tool_args = &call.arguments;
                m_hooks.trigger(ports::HookPoint::PreToolExecution, ctx);
            }
            executeTool(chat_id, call);
            whitelisted_count++;
        } else {
            needs_permission.push_back(std::move(call));
        }
    }

    if (!needs_permission.empty()) {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;
        std::ostringstream uustr;
        uustr << std::hex << dis(gen);
        std::string uuid = uustr.str();

        m_pending_perms[uuid] = {chat_id, model, std::move(needs_permission)};

        if (whitelisted_count > 0)
            m_active_batch = ActiveBatch{"", whitelisted_count, false};

        domain::ToolCallPermissionRequest req;
        req.request_uuid = uuid;
        req.chat_id = chat_id;
        for (auto& c : m_pending_perms[uuid].calls) {
            domain::ToolCallPermissionRequest::CallInfo info;
            info.call_id = c.id;
            info.tool_name = c.name;
            info.arguments = c.arguments;
            req.calls.push_back(std::move(info));
        }
        emitUiEvent(std::move(req));
        return;
    }

    if (whitelisted_count > 0)
        m_active_batch = ActiveBatch{std::move(model), whitelisted_count, true};
}

void DomainActor::handleToolResult(const domain::ActorToolResult& msg) {
    spdlog::info("Actor: ToolResult chat={} call={}", msg.chat_id, msg.call_id);

    domain::ChatMessage tool_msg;
    tool_msg.role = "tool";
    tool_msg.content = msg.result;
    tool_msg.tool_call_id = msg.call_id;
    tool_msg.tool_name = "";
    m_conv->addMessage(msg.chat_id, std::move(tool_msg));

    {
        ports::HookContext ctx;
        ctx.point = ports::HookPoint::PostToolExecution;
        ctx.chat_id = msg.chat_id;
        ctx.tool_result = &tool_msg.content;
        m_hooks.trigger(ports::HookPoint::PostToolExecution, ctx);
    }

    emitUiEvent(domain::ToolCallCompleted{
        .chat_id = msg.chat_id,
        .call_id = msg.call_id,
        .result = tool_msg.content,
        .is_error = msg.is_error,
    });

    if (m_active_batch) {
        m_active_batch->remaining--;
        if (m_active_batch->finalized && m_active_batch->remaining <= 0)
            resumeAfterBatch(msg.chat_id);
    }
}

void DomainActor::resumeAfterBatch(std::string chat_id) {
    if (!m_active_batch) return;

    auto model = std::move(m_active_batch->model);
    m_active_batch.reset();
    runLlm(std::move(chat_id), std::move(model));
}

void DomainActor::executeTool(const std::string& chat_id, const ports::ToolCall& call) {
    auto* inbox = m_inbox.get();
    auto chat = chat_id;

    m_pool->submit([call, this, inbox, chat]() {
        auto result = m_tool_port->execute(call);
        inbox->push(domain::ActorToolResult{
            .chat_id = chat,
            .call_id = call.id,
            .result = result.content,
            .is_error = result.is_error,
        });
    });
}

void DomainActor::handlePermissionRequest(const domain::ActorPermissionRequest& msg) {
    (void)msg;
}

void DomainActor::handlePermissionDecision(const domain::ActorPermissionDecision& msg) {
    spdlog::info("Actor: PermissionDecision uuid={}", msg.request_uuid);

    auto it = m_pending_perms.find(msg.request_uuid);
    if (it == m_pending_perms.end()) return;

    auto pending = std::move(it->second);
    m_pending_perms.erase(it);

    std::unordered_map<std::string, int> decisions;
    for (auto& r : msg.results)
        decisions[r.call_id] = r.decision;

    int newly_executed = 0;
    for (auto& call : pending.calls) {
        auto d = decisions.find(call.id);
        if (d != decisions.end() && d->second != 1) {
            if (d->second == 2 && m_perm_port)
                m_perm_port->addToWhitelist(pending.chat_id, call.name);
            {
                ports::HookContext ctx;
                ctx.point = ports::HookPoint::PreToolExecution;
                ctx.chat_id = pending.chat_id;
                ctx.tool_args = &call.arguments;
                m_hooks.trigger(ports::HookPoint::PreToolExecution, ctx);
            }
            executeTool(pending.chat_id, call);
            newly_executed++;
        } else {
            emitUiEvent(domain::ToolCallCompleted{
                .chat_id = pending.chat_id,
                .call_id = call.id,
                .result = "Tool execution denied by user.",
                .is_error = true,
            });

            domain::ChatMessage tool_msg;
            tool_msg.role = "tool";
            tool_msg.content = "Tool execution denied by user.";
            tool_msg.tool_call_id = call.id;
            tool_msg.tool_name = call.name;
            m_conv->addMessage(pending.chat_id, std::move(tool_msg));
        }
    }

    if (m_active_batch && !m_active_batch->finalized) {
        m_active_batch->model = std::move(pending.model);
        m_active_batch->remaining += newly_executed;
        m_active_batch->finalized = true;

        if (m_active_batch->remaining <= 0)
            resumeAfterBatch(pending.chat_id);
    } else if (newly_executed > 0) {
        m_active_batch = ActiveBatch{std::move(pending.model), newly_executed, true};
    } else {
        runLlm(pending.chat_id, std::move(pending.model));
    }
}

void DomainActor::handlePermissionTimeout(const domain::ActorPermissionTimeout& msg) {
    spdlog::warn("Actor: PermissionTimeout uuid={}", msg.request_uuid);

    auto it = m_pending_perms.find(msg.request_uuid);
    if (it == m_pending_perms.end()) return;

    auto pending = std::move(it->second);
    m_pending_perms.erase(it);

    for (auto& call : pending.calls) {
        emitUiEvent(domain::ToolCallCompleted{
            .chat_id = pending.chat_id,
            .call_id = call.id,
            .result = "Tool execution timed out (no response).",
            .is_error = true,
        });

        domain::ChatMessage tool_msg;
        tool_msg.role = "tool";
        tool_msg.content = "Tool execution timed out.";
        tool_msg.tool_call_id = call.id;
        tool_msg.tool_name = call.name;
        m_conv->addMessage(pending.chat_id, std::move(tool_msg));
    }

    m_active_batch.reset();
    runLlm(pending.chat_id, std::move(pending.model));
}

void DomainActor::handleTitleReady(const domain::ActorTitleReady& msg) {
    spdlog::info("Actor: TitleReady chat={} title={}", msg.chat_id, msg.title);
    m_conv->setTitle(msg.chat_id, msg.title);
    saveActiveConv();

    emitUiEvent(domain::ChatUpdated{
        .chat_id = msg.chat_id,
        .title = msg.title,
    });
    emitSnapshot();
}

void DomainActor::handleCreateChat(const domain::ActorCreateChat& msg) {
    auto conv = m_conv->create(msg.title, msg.model);
    saveActiveConv();
    spdlog::info("Actor: created chat {} '{}'", conv.id, conv.title);

    emitUiEvent(domain::ChatCreated{.chat_id = conv.id});
    emitUiEvent(domain::ChatSelected{.chat_id = conv.id});
    emitSnapshot();
}

void DomainActor::handleDeleteChat(const domain::ActorDeleteChat& msg) {
    if (m_store) m_store->deleteChat(msg.chat_id);
    m_conv->remove(msg.chat_id);
    spdlog::info("Actor: deleted chat {}", msg.chat_id);

    emitUiEvent(domain::ChatDeleted{.chat_id = msg.chat_id});
    emitSnapshot();
}

void DomainActor::handleSelectChat(const domain::ActorSelectChat& msg) {
    m_conv->setActive(msg.chat_id);
    emitUiEvent(domain::ChatSelected{.chat_id = msg.chat_id});

    if (!msg.chat_id.empty()) {
        syncAlwaysOnSkills(msg.chat_id);
    }
}

void DomainActor::handleTogglePin(const domain::ActorTogglePin& msg) {
    m_conv->togglePin(msg.chat_id);
    saveActiveConv();

    emitUiEvent(domain::ChatPinned{
        .chat_id = msg.chat_id,
        .pinned = m_conv->isPinned(msg.chat_id),
    });
    emitSnapshot();
}

void DomainActor::handleRenameChat(const domain::ActorRenameChat& msg) {
    m_conv->setTitle(msg.chat_id, msg.title);
    saveActiveConv();

    emitUiEvent(domain::ChatUpdated{
        .chat_id = msg.chat_id,
        .title = msg.title,
    });
    emitSnapshot();
}

void DomainActor::handleToggleSkill(const domain::ActorToggleSkill& msg) {
    auto ids = m_conv->activeSkillIds(msg.chat_id);

    if (msg.active) {
        if (std::find(ids.begin(), ids.end(), msg.skill_id) == ids.end())
            ids.push_back(msg.skill_id);
    } else {
        std::erase(ids, msg.skill_id);
    }

    m_conv->setActiveSkillIds(msg.chat_id, ids);
    rebuildSystemMessage(msg.chat_id);
}

std::string DomainActor::buildSystemPrompt(std::string_view chat_id) {
    std::string result;
    auto active_ids = m_conv->activeSkillIds(chat_id);

    auto append_skill = [&](const std::string& prompt,
                             const std::string& name,
                             const std::string& description) {
        if (!result.empty()) result += "\n\n";
        result += "[Skill: " + name;
        if (!description.empty()) result += " — " + description;
        result += "]\n\n" + prompt;
    };

    if (m_custom_skills) {
        for (auto& skill : *m_custom_skills) {
            if (skill.prompt.empty()) continue;
            std::string sid = "custom:" + skill.name;
            if (std::find(active_ids.begin(), active_ids.end(), sid) == active_ids.end())
                continue;
            append_skill(skill.prompt, skill.name, skill.description);
        }
    }

    if (m_extensions) {
        for (auto& ext : m_extensions->listInstalled()) {
            for (auto& skill : ext.skills) {
                if (skill.prompt.empty()) continue;
                std::string sid = "ext:" + ext.name + ":" + skill.name;
                if (std::find(active_ids.begin(), active_ids.end(), sid) == active_ids.end())
                    continue;
                append_skill(skill.prompt, skill.name, skill.description);
            }
        }
    }

    return result;
}

void DomainActor::injectSkillsOnFirstMessage(std::string_view chat_id) {
    std::vector<std::string> active_ids;

    if (m_custom_skills) {
        for (auto& skill : *m_custom_skills) {
            if (skill.prompt.empty()) continue;
            if (skill.enabled)
                active_ids.push_back("custom:" + skill.name);
        }
    }
    if (m_extensions) {
        for (auto& ext : m_extensions->listInstalled()) {
            for (auto& skill : ext.skills) {
                if (skill.prompt.empty()) continue;
                if (skill.enabled)
                    active_ids.push_back("ext:" + ext.name + ":" + skill.name);
            }
        }
    }
    m_conv->setActiveSkillIds(chat_id, active_ids);

    auto prompt = buildSystemPrompt(chat_id);
    if (!prompt.empty()) {
        m_conv->setSystemMessage(chat_id, prompt);
    }
}

void DomainActor::rebuildSystemMessage(std::string_view chat_id) {
    auto prompt = buildSystemPrompt(chat_id);
    if (prompt.empty()) return;
    m_conv->updateSystemMessage(chat_id, prompt);
}

void DomainActor::syncAlwaysOnSkills(std::string_view chat_id) {
    auto active_ids = m_conv->activeSkillIds(chat_id);
    auto conv = m_conv->open(chat_id);
    if (conv.messages.empty()) return;

    auto erase_if_stale = [&](const std::string& sid) -> bool {
        if (sid.starts_with("custom:")) {
            if (!m_custom_skills) return true;
            auto name = sid.substr(7);
            return std::find_if(m_custom_skills->begin(), m_custom_skills->end(),
                [&](auto& s) { return s.name == name; })
                == m_custom_skills->end();
        }
        if (sid.starts_with("ext:") && m_extensions) {
            auto rest = sid.substr(4);
            auto dot = rest.find(':');
            if (dot == std::string::npos) return true;
            auto ext_name = rest.substr(0, dot);
            auto skill_name = rest.substr(dot + 1);
            for (auto& ext : m_extensions->listInstalled()) {
                if (ext.name == ext_name) {
                    for (auto& s : ext.skills) {
                        if (s.name == skill_name) return false;
                    }
                }
            }
            return true;
        }
        return false;
    };

    std::erase_if(active_ids, erase_if_stale);

    auto add_if_missing = [&](const std::string& sid) {
        if (std::find(active_ids.begin(), active_ids.end(), sid)
            == active_ids.end())
            active_ids.push_back(sid);
    };

    if (m_custom_skills) {
        for (auto& skill : *m_custom_skills) {
            if (skill.enabled && !skill.prompt.empty())
                add_if_missing("custom:" + skill.name);
        }
    }
    if (m_extensions) {
        for (auto& ext : m_extensions->listInstalled()) {
            for (auto& skill : ext.skills) {
                if (skill.enabled && !skill.prompt.empty())
                    add_if_missing("ext:" + ext.name + ":" + skill.name);
            }
        }
    }

    m_conv->setActiveSkillIds(chat_id, active_ids);
    auto prompt = buildSystemPrompt(chat_id);
    m_conv->updateSystemMessage(chat_id, prompt.empty() ? "" : prompt);
}

void DomainActor::setupAudio(ports::WakewordPort& wakeword,
                              ports::SpeechToTextPort& stt,
                              ports::TextToSpeechPort& tts,
                              ports::AudioDevicePort& audio_device) {
    m_audio_pipeline = std::make_unique<domain::AudioPipeline>(
        wakeword, stt, tts, audio_device);

    m_audio_pipeline->setEvents({
        .on_wake = [this](std::string keyword) {
            domain::ActorWakeDetected msg{std::move(keyword)};
            m_inbox->push(std::move(msg));
        },
        .on_stt_result = [this](std::string transcript, bool is_final,
                                 domain::VoiceMode mode) {
            auto trimmed = transcript;
            trimmed.erase(0, trimmed.find_first_not_of(" \n\r\t"));
            trimmed.erase(trimmed.find_last_not_of(" \n\r\t") + 1);
            if (trimmed.empty()) {
                m_inbox->push(domain::ActorSttEmpty{});
                return;
            }
            if (mode == domain::VoiceMode::LiveChat) {
                auto active = m_conv->active();
                if (!active) return;
                SPDLOG_INFO("Voice: live utterance transcribing '{}' for chat {}",
                            trimmed, active->id);
                m_inbox->push(domain::ActorLiveUtterance{
                    .chat_id = active->id,
                    .transcript = std::move(trimmed),
                    .model = active->model,
                    .is_final = is_final,
                });
            } else {
                SPDLOG_INFO("Voice: wakeword query '{}'", trimmed);
                m_inbox->push(domain::ActorWakeQuery{
                    .transcript = std::move(trimmed),
                    .is_final = is_final,
                });
            }
        },
        .on_tts_done = [this]() {
            m_inbox->push(domain::ActorTtsFinished{});
        },
        .on_state_change = [this](ports::AudioState old_state,
                                   ports::AudioState new_state) {
            emitUiEvent(domain::AudioStateChanged{
                static_cast<int>(old_state),
                static_cast<int>(new_state)
            });
        },
    });
}

void DomainActor::enableVoice() {
    if (m_audio_pipeline) m_audio_pipeline->enable();
}

void DomainActor::disableVoice() {
    if (m_audio_pipeline) m_audio_pipeline->disable();
}

void DomainActor::muteVoice() {
    if (m_audio_pipeline) m_audio_pipeline->mute();
}

void DomainActor::unmuteVoice() {
    if (m_audio_pipeline) m_audio_pipeline->unmute();
}

bool DomainActor::isVoiceEnabled() const {
    return m_audio_pipeline && m_audio_pipeline->isVoiceEnabled();
}

bool DomainActor::isVoiceMuted() const {
    return m_audio_pipeline && m_audio_pipeline->isMuted();
}

void DomainActor::handleVoiceToggle(const domain::ActorVoiceToggle& msg) {
    if (!m_audio_pipeline) return;
    if (msg.enabled)
        m_audio_pipeline->enable();
    else
        m_audio_pipeline->disable();
}

void DomainActor::handleVoiceMute(const domain::ActorVoiceMute& msg) {
    if (!m_audio_pipeline) return;
    if (msg.muted)
        m_audio_pipeline->mute();
    else
        m_audio_pipeline->unmute();
}

void DomainActor::handleWakeDetected(const domain::ActorWakeDetected& msg) {
    emitUiEvent(domain::AudioWakeDetected{msg.keyword});
}

void DomainActor::handleWakeQuery(const domain::ActorWakeQuery& msg) {
    emitUiEvent(domain::UserInputReceived{
        .chat_id = "",
        .content = msg.transcript,
        .source = "voice_wake",
        .model = "",
    });

    auto temp_conv = m_conv->openEphemeral(m_llm ? "default" : "");
    if (!temp_conv.has_value()) return;
    auto chat_id = temp_conv->id;

    m_voice_triggered_chats.insert(chat_id);

    domain::ChatMessage user_msg;
    user_msg.role = "user";
    user_msg.content = msg.transcript;
    m_conv->addMessage(chat_id, user_msg);

    if (m_llm) {
        runLlm(chat_id, temp_conv->model);
    }
}

void DomainActor::handleLiveUtterance(const domain::ActorLiveUtterance& msg) {
    emitUiEvent(domain::UserInputReceived{
        .chat_id = msg.chat_id,
        .content = msg.transcript,
        .source = "voice_live",
        .model = msg.model,
    });

    m_voice_triggered_chats.insert(msg.chat_id);

    domain::ActorUserInput input_msg{
        .chat_id = msg.chat_id,
        .content = msg.transcript,
        .model = msg.model,
    };
    handleUserInput(input_msg);
}

void DomainActor::handleTtsFinished(const domain::ActorTtsFinished&) {
    emitUiEvent(domain::TtsCompleted{});
}

void DomainActor::handleSttEmpty(const domain::ActorSttEmpty&) {
    SPDLOG_INFO("Voice: STT produced empty transcript, recovering pipeline");
    if (m_audio_pipeline)
        m_audio_pipeline->onResponseReady("");
}

void DomainActor::handleVoiceLiveToggle(const domain::ActorVoiceLiveToggle& msg) {
    if (!m_audio_pipeline) return;
    if (msg.enabled) {
        SPDLOG_INFO("Voice: live-chat mode enabled for chat {}", msg.chat_id);
        m_audio_pipeline->startLiveMode();
    } else {
        SPDLOG_INFO("Voice: live-chat mode ended for chat {}", msg.chat_id);
        m_audio_pipeline->stopLiveMode();
    }
}

void DomainActor::handleBargeIn(const domain::ActorBargeIn&) {
    if (m_audio_pipeline)
        m_audio_pipeline->onBargeInDetected();
}

void DomainActor::saveActiveConv() {
    if (!m_store) return;
    m_conv->saveActiveConversation();
}

} // namespace rook::core
