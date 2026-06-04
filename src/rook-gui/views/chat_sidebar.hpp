#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/GLib/functions.h>
#include <peel/class.h>
#include <string_view>
#include "rook/domain/events.hpp"
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"

namespace rook::core { class DomainActor; }

namespace rook::gui {

class ChatSidebar final : public peel::Gtk::Box
{
    PEEL_SIMPLE_CLASS(ChatSidebar, peel::Gtk::Box)

    rook::domain::EventBus *m_bus = nullptr;
    rook::core::DomainActor *m_actor = nullptr;
    rook::domain::SnapshotReady m_snapshot;

    peel::Gtk::SearchEntry *m_search = nullptr;
    peel::Gtk::ListBox *m_list = nullptr;
    peel::Gtk::Box *m_empty_placeholder = nullptr;
    peel::Gtk::Box *m_no_results_placeholder = nullptr;
    peel::Gtk::ListBoxRow *m_rename_row = nullptr;

    rook::domain::EventBus::HandlerId m_created_handler;
    rook::domain::EventBus::HandlerId m_deleted_handler;
    rook::domain::EventBus::HandlerId m_updated_handler;
    rook::domain::EventBus::HandlerId m_selected_handler;
    rook::domain::EventBus::HandlerId m_pinned_handler;
    rook::domain::EventBus::HandlerId m_snapshot_handler;

    inline void init(Class *);
    inline void vfunc_dispose ();

    void onNewChat(peel::Gtk::Button *);
    void onRowActivated(peel::Gtk::ListBox *, peel::Gtk::ListBoxRow *row);
    void onSearchChanged();

    void onChatCreated(const rook::domain::ChatCreated &event);
    void onChatDeleted(const rook::domain::ChatDeleted &event);
    void onChatUpdated(const rook::domain::ChatUpdated &event);
    void onChatSelected(const rook::domain::ChatSelected &event);
    void onChatPinned(const rook::domain::ChatPinned &event);
    void onSnapshot(const rook::domain::SnapshotReady &event);

    void rebuildList();
    peel::Gtk::ListBoxRow* buildChatRow(std::string_view id, std::string title,
                                         bool pinned);
    void showContextMenu(::GtkWidget *parent, std::string_view chat_id);

    void startRename(std::string_view chat_id);
    void confirmRename(std::string_view chat_id, std::string_view new_title);
    void cancelRename();
    void confirmDelete(std::string_view chat_id);

    static void onContextGesture(GtkGestureClick *, int n_press,
                                  double x, double y, gpointer data);

public:
    static peel::FloatPtr<ChatSidebar> create(rook::domain::EventBus &bus,
                                               rook::core::DomainActor *actor);

    void loadConversations(const std::vector<rook::domain::Conversation> &chats);
};

} // namespace rook::gui
