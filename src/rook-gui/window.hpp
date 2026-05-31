#pragma once

#include <gtkmm.h>
#include <adwaita.h>
#include <giomm.h>
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class ChatView;
class ChatSidebar;
class MessageWidget;

class RookWindow : public Gtk::Window {
public:
    explicit RookWindow(rook::domain::EventBus& bus, rook::ports::LlmPort& llm,
                        rook::domain::ConversationManager& conversations,
                        sigc::slot<void()> on_settings_changed = {});
    ~RookWindow() override = default;

    void refreshModels();

private:
    void setupHeaderBar();
    void setupLayout(rook::domain::ConversationManager& conversations);
    void setupActions();
    void onSettings();
    void onAbout();

    rook::domain::EventBus& m_bus;
    rook::ports::LlmPort& m_llm;
    sigc::slot<void()> m_on_settings_changed;
    Glib::RefPtr<Gio::SimpleActionGroup> m_action_group;
    Gtk::HeaderBar m_header_bar;
    Gtk::Paned m_paned;
    ChatSidebar* m_sidebar = nullptr;
    ChatView* m_chat_view = nullptr;
};

} // namespace rook::gui
