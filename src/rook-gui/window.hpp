#pragma once

#include <peel/Adw/Adw.h>
#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <functional>
#include <string_view>
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class ChatSidebar;
class ChatView;

class RookWindow final : public peel::Adw::ApplicationWindow
{
    PEEL_SIMPLE_CLASS(RookWindow, peel::Adw::ApplicationWindow)

    peel::FloatPtr<peel::Adw::HeaderBar> m_header;
    peel::FloatPtr<peel::Adw::NavigationSplitView> m_split;
    ChatSidebar *m_sidebar = nullptr;
    ChatView *m_chat_view = nullptr;
    std::function<void()> m_save_fn;

    inline void init(Class *);

public:
    void showPreferences() { onPreferences(); }
    void showAbout() { onAbout(); }

    static RookWindow *create(peel::Gtk::Application *app,
                               rook::domain::EventBus &bus,
                               rook::ports::LlmPort &llm,
                               rook::domain::ConversationManager &conv,
                               std::function<void()> save_fn);

    void refreshModels();

private:
    void onPreferences();
    void onAbout();
};

} // namespace rook::gui
