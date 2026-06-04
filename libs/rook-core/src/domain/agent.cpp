#include "rook/domain/agent.hpp"
#include "rook/ports/extension_port.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace rook::domain {

AgentEngine::AgentEngine(EventBus& bus, ports::LlmPort& llm,
                         ConversationManager& conv, ports::ToolPort& tool,
                         ports::ExtensionPort* extensions,
                         std::vector<rook::adapters::extension::CustomSkill>* custom_skills)
    : m_bus(bus)
    , m_llm(llm)
    , m_conv(conv)
    , m_tool(tool)
    , m_extensions(extensions)
    , m_custom_skills(custom_skills)
{}

void AgentEngine::start() {
    m_input_handler = m_bus.subscribe<UserInputReceived>(
        [this](const UserInputReceived& event) { onUserInput(event); });

    m_chunk_handler = m_bus.subscribe<LlmStreamChunk>(
        [this](const LlmStreamChunk& event) { onLlmChunk(event); });

    m_completed_handler = m_bus.subscribe<LlmCompleted>(
        [this](const LlmCompleted& event) { onLlmCompleted(event); });

    m_error_handler = m_bus.subscribe<LlmError>(
        [this](const LlmError& event) { onLlmError(event); });

    m_tool_handler = m_bus.subscribe<ToolCallCompleted>(
        [this](const ToolCallCompleted& event) { onToolCallCompleted(event); });

    m_skill_handler = m_bus.subscribe<SkillToggled>(
        [this](const SkillToggled& event) { onSkillToggled(event); });

    m_chat_selected_handler = m_bus.subscribe<ChatSelected>(
        [this](const ChatSelected& event) { onChatSelected(event); });

    spdlog::info("AgentEngine started");
}

void AgentEngine::onUserInput(const UserInputReceived& event) {
    spdlog::info("UserInput: chat={}, source={}", event.chat_id, event.source);

    ChatMessage msg;
    msg.role = "user";
    msg.content = event.content;
    m_conv.setModel(event.chat_id, event.model);

    auto conv = m_conv.open(event.chat_id);
    bool is_first = conv.messages.empty();
    size_t msg_count = conv.messages.size();
    spdlog::info("onUserInput: chat={} messages_before={} is_first={}",
        event.chat_id, msg_count, is_first);
    m_conv.addMessage(event.chat_id, std::move(msg));

    if (is_first) {
        injectSkillsOnFirstMessage(event.chat_id);
    } else {
        syncAlwaysOnSkills(event.chat_id);
    }

    m_pending_tool_calls.clear();
    runLlm(event.chat_id, event.model);
}

static std::string buildToolsJson(const std::vector<ports::ToolDefinition>& tools) {
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
        if (!required.empty()) {
            tool["function"]["parameters"]["required"] = required;
        }

        arr.push_back(std::move(tool));
    }
    return arr.dump();
}

void AgentEngine::runLlm(std::string chat_id, std::string model) {
    spdlog::info("runLlm: start chat={} model={}", chat_id, model);

    auto messages = m_conv.buildLlmMessages(chat_id);
    spdlog::info("runLlm: {} messages", messages.size());

    if (m_extensions) {
        for (auto& msg : messages) {
            if (msg.role == "user" && msg.content.starts_with("/")) {
                auto space_pos = msg.content.find(' ');
                auto cmd_name = space_pos != std::string::npos
                    ? msg.content.substr(1, space_pos - 1)
                    : msg.content.substr(1);
                auto remainder = space_pos != std::string::npos
                    ? msg.content.substr(space_pos + 1)
                    : std::string{};

                bool found = false;
                for (auto& ext : m_extensions->listInstalled()) {
                    for (auto& cmd : ext.commands) {
                        if (cmd.name == cmd_name) {
                            spdlog::info("AgentEngine: expanding command /{}", cmd_name);
                            if (!remainder.empty())
                                msg.content = cmd.prompt + "\n\nUser input: " + remainder;
                            else
                                msg.content = cmd.prompt;
                            found = true;
                            break;
                        }
                    }
                    if (found) break;
                }
            }
        }
    }

    auto tools = m_tool.listTools();
    auto tools_json = buildToolsJson(tools);

    spdlog::info("runLlm: {} tools, calling streamChat", tools.size());

    m_bus.publish(LlmRequested{chat_id, ""});

    m_llm.streamChat(
        chat_id,
        messages,
        [this, chat_id](std::string_view chunk, bool is_final, bool is_reasoning) {
            if (!chunk.empty()) {
                m_bus.publish(LlmStreamChunk{
                    .chat_id = chat_id,
                    .content = std::string(chunk),
                    .is_final = is_final,
                    .is_reasoning = is_reasoning,
                });
            }

            if (is_final && !is_reasoning) {
                spdlog::info("AgentEngine: publishing LlmCompleted for {}", chat_id);
                m_bus.publish(LlmCompleted{chat_id, 0});
            }
        },
        model,
        [this, chat_id](std::string_view name, std::string_view arguments,
                         std::string_view call_id) {
            ports::ToolCall call;
            call.id = std::string(call_id);
            call.name = std::string(name);
            call.arguments = std::string(arguments);

            m_bus.publish(ToolCallRequested{
                .chat_id = chat_id,
                .tool_name = call.name,
                .arguments = call.arguments,
                .call_id = call.id,
            });

            m_pending_tool_calls.push_back(std::move(call));
        },
        tools_json
    );

    spdlog::info("runLlm: streamChat returned for chat {}", chat_id);
}

void AgentEngine::onLlmChunk(const LlmStreamChunk& chunk) {
    if (chunk.chat_id.empty() || chunk.chat_id.starts_with("__title__")) return;

    if (chunk.is_reasoning) {
        m_conv.updateReasoningChunk(chunk.chat_id, chunk.content);
    } else {
        m_conv.updateAssistantChunk(chunk.chat_id, chunk.content);
    }
}

void AgentEngine::onLlmCompleted(const LlmCompleted& event) {
    spdlog::info("LLM response complete for chat {}", event.chat_id);

    if (processPendingToolCalls(event.chat_id)) return;

    m_conv.saveActiveConversation();

    auto conv = m_conv.open(event.chat_id);
    if (conv.title != "New Chat") return;

    int user_count = 0;
    int assistant_count = 0;
    std::string last_user;
    std::string last_assistant;

    for (const auto& msg : conv.messages) {
        if (msg.role == "user") { user_count++; last_user = msg.content; }
        if (msg.role == "assistant") { assistant_count++; last_assistant = msg.content; }
    }

    if (user_count != 1 || assistant_count != 1) return;

    spdlog::info("Generating title for chat {}", event.chat_id);

    ports::LlmMessage sys_msg;
    sys_msg.role = "system";
    sys_msg.content = "You are a title generator. Reply with ONLY the raw title text, "
                      "no quotes, no prefixes, no markdown, no explanation.";

    ports::LlmMessage title_msg;
    title_msg.role = "user";
    title_msg.content = "Generate a short title (max 5 words) in the same language "
                        "for this conversation:\n\n"
                        "User: " + last_user + "\n"
                        "Assistant: " + last_assistant;

    std::string accumulated;
    std::string chat_id = event.chat_id;
    std::string model = conv.model;

    auto cleanTitle = [](std::string s) -> std::string {
        s.erase(0, s.find_first_not_of(" \t\n\r\"'"));
        s.erase(s.find_last_not_of(" \t\n\r\"'") + 1);
        for (auto prefix : {"Title:", "title:", "Title :", "title :",
                            "**", "##", "#"}) {
            if (s.starts_with(prefix)) { s.erase(0, std::strlen(prefix)); break; }
        }
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    };

    m_llm.streamChat(
        chat_id,
        {sys_msg, title_msg},
        [this, chat_id, &accumulated, &cleanTitle](std::string_view chunk, bool is_final, bool is_reasoning) {
            if (is_reasoning) return;
            accumulated += std::string(chunk);
            if (is_final && !accumulated.empty()) {
                std::string title = cleanTitle(accumulated);
                if (!title.empty()) {
                    m_conv.setTitle(chat_id, std::move(title));
                }
            }
        },
        model
    );
}

bool AgentEngine::processPendingToolCalls(std::string_view chat_id) {
    if (m_pending_tool_calls.empty()) return false;

    spdlog::info("Processing {} tool calls for chat {}", m_pending_tool_calls.size(), chat_id);

    auto conv = m_conv.open(chat_id);
    std::string model = conv.model;

    nlohmann::json tc_json = nlohmann::json::array();
    for (auto& tc : m_pending_tool_calls) {
        nlohmann::json item;
        item["id"] = tc.id;
        item["type"] = "function";
        item["function"]["name"] = tc.name;
        item["function"]["arguments"] = tc.arguments;
        tc_json.push_back(std::move(item));
    }

    m_conv.setAssistantToolCalls(chat_id, tc_json.dump());

    auto calls = std::move(m_pending_tool_calls);
    m_pending_tool_calls.clear();

    for (auto& call : calls) {
        auto result = m_tool.execute(call);

        m_bus.publish(ToolCallCompleted{
            .chat_id = std::string(chat_id),
            .call_id = call.id,
            .result = result.content,
            .is_error = result.is_error,
        });

        ChatMessage tool_msg;
        tool_msg.role = "tool";
        tool_msg.content = result.content;
        tool_msg.tool_call_id = call.id;
        tool_msg.tool_name = call.name;
        m_conv.addMessage(chat_id, std::move(tool_msg));

        spdlog::info("Tool {} completed: {}", call.name,
                     result.is_error ? "error" : "success");
    }

    runLlm(std::string(chat_id), std::move(model));
    return true;
}

void AgentEngine::onLlmError(const LlmError& event) {
    spdlog::error("LLM error in chat {}: {}", event.chat_id, event.message);
}

void AgentEngine::onToolCallCompleted(const ToolCallCompleted& /*event*/) {
    spdlog::info("Tool call completed");
}

std::string AgentEngine::buildSystemPrompt(std::string_view chat_id) {
    std::string result;

    auto active_ids = m_conv.activeSkillIds(chat_id);

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
            bool is_active = std::find(active_ids.begin(), active_ids.end(), sid)
                          != active_ids.end();
            if (!is_active) continue;
            append_skill(skill.prompt, skill.name, skill.description);
        }
    }

    if (m_extensions) {
        for (auto& ext : m_extensions->listInstalled()) {
            for (auto& skill : ext.skills) {
                if (skill.prompt.empty()) continue;
                std::string sid = "ext:" + ext.name + ":" + skill.name;
                bool is_active = std::find(active_ids.begin(), active_ids.end(), sid)
                              != active_ids.end();
                if (!is_active) continue;
                append_skill(skill.prompt, skill.name, skill.description);
            }
        }
    }

    if (m_extensions) {
        std::string context_index;
        for (auto& ext : m_extensions->listInstalled()) {
            if (ext.context_files.empty()) continue;
            context_index += "\nFrom " + ext.display_name
                + " v" + ext.version + ":\n";
            for (auto& cf : ext.context_files) {
                context_index += "  " + ext.install_path
                    + "/" + cf.path;
                if (!cf.description.empty())
                    context_index += " — " + cf.description;
                context_index += "\n";
            }
        }
        if (!context_index.empty()) {
            if (!result.empty()) result += "\n\n";
            result += "Available extension context files (use read_file to access):"
                   + context_index;
        }
    }

    return result;
}

void AgentEngine::injectSkillsOnFirstMessage(std::string_view chat_id) {
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
    m_conv.setActiveSkillIds(chat_id, active_ids);

    for (auto& sid : active_ids) {
        m_bus.publish(SkillToggled{
            .chat_id = std::string(chat_id),
            .skill_id = sid,
            .active = true,
        });
    }

    auto prompt = buildSystemPrompt(chat_id);
    if (!prompt.empty()) {
        m_conv.setSystemMessage(chat_id, prompt);
    }
}

void AgentEngine::rebuildSystemMessage(std::string_view chat_id) {
    auto prompt = buildSystemPrompt(chat_id);
    if (prompt.empty()) return;
    m_conv.updateSystemMessage(chat_id, prompt);
}

void AgentEngine::onSkillToggled(const SkillToggled& event) {
    if (!m_extensions || event.chat_id.empty()) return;

    auto active_ids = m_conv.activeSkillIds(event.chat_id);
    if (event.active) {
        if (std::find(active_ids.begin(), active_ids.end(), event.skill_id)
            == active_ids.end())
            active_ids.push_back(event.skill_id);
    } else {
        std::erase(active_ids, event.skill_id);
    }
    m_conv.setActiveSkillIds(event.chat_id, active_ids);

    auto prompt = buildSystemPrompt(event.chat_id);
    if (!prompt.empty()) {
        m_conv.updateSystemMessage(event.chat_id, prompt);
    }
}

void AgentEngine::onChatSelected(const ChatSelected& event) {
    if (!event.chat_id.empty()) {
        syncAlwaysOnSkills(event.chat_id);
    }
}

void AgentEngine::syncAlwaysOnSkills(std::string_view chat_id) {
    auto active_ids = m_conv.activeSkillIds(chat_id);
    auto conv = m_conv.open(chat_id);
    if (conv.messages.empty()) return;

    auto erase_if_stale = [&](const std::string& sid) -> bool {
        if (sid.starts_with("custom:")) {
            if (!m_custom_skills) return true;
            auto name = sid.substr(7);
            return std::find_if(m_custom_skills->begin(),
                m_custom_skills->end(),
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
            == active_ids.end()) {
            active_ids.push_back(sid);
        }
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

    m_conv.setActiveSkillIds(chat_id, active_ids);
    auto prompt = buildSystemPrompt(chat_id);
    m_conv.updateSystemMessage(chat_id, prompt.empty() ? "" : prompt);
}

} // namespace rook::domain
