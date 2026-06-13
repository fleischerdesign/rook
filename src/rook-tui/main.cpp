#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "rook/core/domain_actor.hpp"
#include "rook/core/settings.hpp"
#include "rook/core/actor_messages.hpp"
#include "rook/domain/events.hpp"
#include "rook/adapters/store/json_store.hpp"
#include "rook/adapters/secret_store.hpp"
#include "rook/adapters/llm/llm_factory.hpp"
#include "rook/adapters/llm/multi_provider_adapter.hpp"
#include "rook/adapters/composite/composite_tool_port.hpp"
#include "rook/adapters/builtin/builtin_tool_port.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>

using namespace ftxui;

namespace {

struct ChatItem {
    std::string id;
    std::string title;
    bool pinned = false;
};

struct MessageLine {
    std::string role;
    std::string content;
};

class TuiState {
public:
    std::vector<ChatItem> chats;
    int selected_chat = -1;
    std::string active_chat_id;

    std::deque<MessageLine> messages;
    bool streaming = false;
    std::string stream_buffer;

    std::string input_buffer;

    std::string status_text = "Ready";
    bool processing = false;

    rook::core::DomainActor* actor = nullptr;

    std::mutex mtx;

    void clearMessages()
    {
        std::lock_guard lock(mtx);
        messages.clear();
        stream_buffer.clear();
        streaming = false;
    }

    void addMessage(const std::string& role, const std::string& content)
    {
        std::lock_guard lock(mtx);
        messages.push_back({role, content});
    }

    void appendStream(const std::string& content)
    {
        std::lock_guard lock(mtx);
        streaming = true;
        stream_buffer += content;
    }

    void finalizeStream()
    {
        std::lock_guard lock(mtx);
        if (streaming && !stream_buffer.empty()) {
            messages.push_back({"assistant", stream_buffer});
            stream_buffer.clear();
        }
        streaming = false;
    }
};

void handleDomainEvent(TuiState& state, rook::domain::DomainEvent event)
{
    std::visit([&](auto&& ev) {
        using T = std::decay_t<decltype(ev)>;

        if constexpr (std::is_same_v<T, rook::domain::LlmStreamChunk>) {
            if (ev.chat_id == state.active_chat_id) {
                state.appendStream(ev.content);
                if (ev.is_final) {
                    state.finalizeStream();
                    state.processing = false;
                    state.status_text = "Ready";
                }
            }
        }
        else if constexpr (std::is_same_v<T, rook::domain::LlmCompleted>) {
            if (ev.chat_id == state.active_chat_id) {
                state.processing = false;
                state.status_text = "Ready";
            }
        }
        else if constexpr (std::is_same_v<T, rook::domain::LlmError>) {
            if (ev.chat_id == state.active_chat_id) {
                state.addMessage("error", ev.message);
                state.finalizeStream();
                state.processing = false;
                state.status_text = "Error: " + ev.message;
            }
        }
        else if constexpr (std::is_same_v<T, rook::domain::ChatCreated>) {
            state.chats.push_back({ev.chat_id, ev.chat_id.substr(0, 20)});
            if (state.selected_chat < 0)
                state.selected_chat = static_cast<int>(state.chats.size()) - 1;
        }
        else if constexpr (std::is_same_v<T, rook::domain::ChatSelected>) {
            state.active_chat_id = ev.chat_id;
        }
        else if constexpr (std::is_same_v<T, rook::domain::SnapshotReady>) {
            std::lock_guard lock(state.mtx);
            state.chats.clear();
            for (auto& conv : ev.conversations) {
                state.chats.push_back({conv.id, conv.title, conv.pinned});
            }
            state.active_chat_id = ev.active_chat_id;
            if (!ev.active_chat_id.empty()) {
                for (int i = 0; i < static_cast<int>(state.chats.size()); ++i) {
                    if (state.chats[i].id == ev.active_chat_id) {
                        state.selected_chat = i;
                        break;
                    }
                }
            }
        }
    }, event);
}

Element renderMessage(const MessageLine& msg)
{
    Color role_color = msg.role == "user"
        ? Color::Cyan
        : msg.role == "assistant"
            ? Color::Green
            : Color::Red;

    auto role_elem = text("[" + msg.role + "]") | ftxui::color(role_color) | bold;

    return vbox({
        role_elem,
        paragraph(msg.content),
        separator(),
    });
}

} // anonymous namespace

int main(int, char**)
{
    auto null_logger = spdlog::null_logger_mt("null-tui");
    spdlog::set_default_logger(null_logger);

    const char* home = std::getenv("HOME");
    std::string data_dir = home
        ? std::string(home) + "/.local/share/rook"
        : "/tmp/rook-data";

    auto store_ptr = rook::adapters::store::makeJsonStore(data_dir);
    auto& store = *store_ptr;

    auto secrets = std::make_unique<rook::adapters::SecretStore>(
        "io.github.fleischerdesign.Rook");

    auto llm_ptr = rook::adapters::llm::makeMultiProviderAdapter();
    auto& llm = *llm_ptr;

    auto tool_ptr = std::make_unique<rook::adapters::composite::CompositeToolPort>();
    auto& tool = *tool_ptr;

    rook::core::SettingsLoader settings;
    settings.load(store, llm, *secrets);

    auto actor = std::make_unique<rook::core::DomainActor>();

    TuiState state;
    state.actor = actor.get();

    actor->start(llm, tool,
        nullptr,
        &store,
        [&state](rook::domain::DomainEvent event) {
            handleDomainEvent(state, std::move(event));
        },
        nullptr, nullptr);

    if (state.chats.empty()) {
        state.chats.push_back({"default", "New Chat"});
        state.selected_chat = 0;
    }

    auto screen = ScreenInteractive::Fullscreen();

    auto sidebar_renderer = Renderer([&] {
        Elements items;
        for (int i = 0; i < static_cast<int>(state.chats.size()); ++i) {
            auto& chat = state.chats[i];
            auto prefix = chat.pinned ? std::string("* ") : std::string("  ");
            auto elem = text(prefix + chat.title);
            if (i == state.selected_chat) {
                elem = elem | inverted;
            }
            items.push_back(elem);
        }

        if (items.empty()) {
            items.push_back(text("No chats") | dim);
        }

        return vbox({
            text("Chats") | bold | center,
            separator(),
            vbox(std::move(items)) | flex,
        }) | border | size(WIDTH, EQUAL, 25);
    });

    auto messages_renderer = Renderer([&] {
        Elements msg_elements;
        {
            std::lock_guard lock(state.mtx);
            for (auto& msg : state.messages) {
                msg_elements.push_back(renderMessage(msg));
            }
            if (state.streaming && !state.stream_buffer.empty()) {
                msg_elements.push_back(renderMessage(
                    {"assistant", state.stream_buffer}));
            }
        }

        if (msg_elements.empty()) {
            msg_elements.push_back(text("Start a conversation...") | dim | center);
        }

        return vbox(std::move(msg_elements)) | flex | frame | vscroll_indicator;
    });

    Component input_component = Input(&state.input_buffer, "Type a message...");
    Component send_button = Button("Send", [&] {
        if (state.input_buffer.empty() || !state.actor)
            return;

        state.processing = true;
        state.status_text = "Processing...";

        std::string text = state.input_buffer;

        if (state.active_chat_id.empty() && !state.chats.empty()) {
            state.active_chat_id = state.chats[state.selected_chat].id;
        }

        state.addMessage("user", text);
        state.input_buffer.clear();

        state.actor->post(rook::domain::ActorUserInput{
            state.active_chat_id.empty() ? "default" : state.active_chat_id,
            text,
            "",
        });
    });

    auto input_container = Container::Horizontal({
        input_component,
        send_button,
    });

    auto input_renderer = Renderer(input_container, [&] {
        return hbox({
            input_container->Render() | flex,
            separatorEmpty(),
            send_button->Render(),
        });
    });

    auto status_renderer = Renderer([&] {
        auto color = state.processing ? Color::Yellow : Color::Green;
        return text(state.status_text) | ftxui::color(color);
    });

    auto main_container = Container::Vertical({
        input_container,
    });

    auto main_renderer = Renderer(main_container, [&] {
        return vbox({
            messages_renderer->Render() | flex,
            separator(),
            input_renderer->Render(),
            separator(),
            status_renderer->Render(),
        }) | border;
    });

    auto layout = Container::Horizontal({
        sidebar_renderer,
        main_container,
    });

    auto layout_renderer = Renderer(layout, [&] {
        return hbox({
            sidebar_renderer->Render(),
            separator(),
            main_renderer->Render() | flex,
        });
    });

    auto component = CatchEvent(layout_renderer, [&](Event event) {
        if (event == Event::Character('q')) {
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Character('n')) {
            if (state.actor) {
                state.actor->post(rook::domain::ActorCreateChat{"", ""});
            }
            state.clearMessages();
            state.status_text = "New chat";
            return true;
        }
        if (event == Event::Character('j')) {
            if (state.selected_chat < static_cast<int>(state.chats.size()) - 1) {
                state.selected_chat++;
                if (state.selected_chat >= 0
                    && state.selected_chat < static_cast<int>(state.chats.size()))
                {
                    auto& chat = state.chats[state.selected_chat];
                    if (state.actor) {
                        state.actor->post(
                            rook::domain::ActorSelectChat{chat.id});
                    }
                }
            }
            return true;
        }
        if (event == Event::Character('k')) {
            if (state.selected_chat > 0) {
                state.selected_chat--;
                if (state.selected_chat >= 0
                    && state.selected_chat < static_cast<int>(state.chats.size()))
                {
                    auto& chat = state.chats[state.selected_chat];
                    if (state.actor) {
                        state.actor->post(
                            rook::domain::ActorSelectChat{chat.id});
                    }
                }
            }
            return true;
        }
        return false;
    });

    std::atomic<bool> refresh{true};
    std::thread refresh_thread([&] {
        while (refresh.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            screen.PostEvent(Event::Custom);
        }
    });

    screen.Loop(component);

    refresh.store(false, std::memory_order_release);
    refresh_thread.join();

    actor->stop();

    return 0;
}
