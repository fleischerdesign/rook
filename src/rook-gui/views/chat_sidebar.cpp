#include <glib/gi18n.h>
#include "chat_sidebar.hpp"
#include "rook/core/domain_actor.hpp"
#include "rook/core/actor_messages.hpp"
#include <spdlog/spdlog.h>
#include <peel/Adw/Adw.h>
#include <peel/Gtk/Orientable.h>
#include <peel/Graphene/Rect.h>
#include <peel/Gdk/Rectangle.h>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(ChatSidebar, "RookChatSidebar", Gtk::Box)

inline void ChatSidebar::Class::init()
{
    override_vfunc_dispose<ChatSidebar>();
}

inline void ChatSidebar::init(Class *)
{
    new (&m_snapshot) rook::domain::SnapshotReady();
    reinterpret_cast<Gtk::Orientable*>(this)->set_orientation(Gtk::Orientation::VERTICAL);
    set_size_request(220, -1);

    auto search = Gtk::SearchEntry::create();
    search->set_placeholder_text(_("Search chats..."));
    search->set_margin_start(6);
    search->set_margin_end(6);
    search->set_margin_top(6);
    search->set_margin_bottom(3);
    search->connect_search_changed([this](Gtk::SearchEntry *) { onSearchChanged(); });
    m_search = search;
    append(std::move(search));

    auto scrolled = Gtk::ScrolledWindow::create();
    auto list = Gtk::ListBox::create();
    list->add_css_class("navigation-sidebar");
    list->set_hexpand(true);
    list->set_vexpand(true);
    list->connect_row_activated(
        [this](Gtk::ListBox *, Gtk::ListBoxRow *r) { onRowActivated(nullptr, r); });
    m_list = list;
    scrolled->set_child(std::move(list));

    auto empty_box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 6);
    empty_box->set_valign(Gtk::Align::CENTER);
    empty_box->set_halign(Gtk::Align::CENTER);
    empty_box->set_vexpand(true);
    empty_box->set_hexpand(true);
    auto empty_label = Gtk::Label::create(_("No conversations yet"));
    empty_label->add_css_class("dim-label");
    empty_box->append(std::move(empty_label));
    m_empty_placeholder = empty_box;
    m_empty_placeholder->set_visible(false);

    auto nores_box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 6);
    nores_box->set_valign(Gtk::Align::CENTER);
    nores_box->set_halign(Gtk::Align::CENTER);
    nores_box->set_vexpand(true);
    nores_box->set_hexpand(true);
    auto nores_label = Gtk::Label::create(_("No matching chats"));
    nores_label->add_css_class("dim-label");
    nores_box->append(std::move(nores_label));
    m_no_results_placeholder = nores_box;
    m_no_results_placeholder->set_visible(false);

    auto overlay = Gtk::Overlay::create();
    overlay->set_vexpand(true);
    auto eb = static_cast<Gtk::Widget*>(static_cast<Gtk::Box*>(empty_box));
    auto nb = static_cast<Gtk::Widget*>(static_cast<Gtk::Box*>(nores_box));
    auto sw = static_cast<Gtk::Widget*>(static_cast<Gtk::ScrolledWindow*>(scrolled));
    overlay->add_overlay(eb);
    overlay->add_overlay(nb);
    overlay->set_child(sw);
    append(std::move(overlay));

    auto new_btn = Gtk::Button::create_with_label(_("New Chat"));
    new_btn->add_css_class("suggested-action");
    new_btn->set_margin_start(6);
    new_btn->set_margin_end(6);
    new_btn->set_margin_top(3);
    new_btn->set_margin_bottom(6);
    new_btn->connect_clicked([this](Gtk::Button *) { onNewChat(nullptr); });
    append(std::move(new_btn));
}

inline void ChatSidebar::vfunc_dispose()
{
    if (m_bus) {
        m_bus->unsubscribe(m_created_handler);
        m_bus->unsubscribe(m_deleted_handler);
        m_bus->unsubscribe(m_updated_handler);
        m_bus->unsubscribe(m_selected_handler);
        m_bus->unsubscribe(m_pinned_handler);
        m_bus->unsubscribe(m_snapshot_handler);
        m_bus = nullptr;
    }
    m_snapshot.~SnapshotReady();
    parent_vfunc_dispose<ChatSidebar>();
}

FloatPtr<ChatSidebar> ChatSidebar::create(rook::domain::EventBus &bus,
                                           rook::core::DomainActor *actor)
{
    auto sidebar = Object::create<ChatSidebar>();
    auto *s = static_cast<ChatSidebar*>(sidebar);
    s->m_bus = &bus;
    s->m_actor = actor;

    s->m_created_handler = bus.subscribe<rook::domain::ChatCreated>(
        [s](const rook::domain::ChatCreated &event) { s->onChatCreated(event); });
    s->m_deleted_handler = bus.subscribe<rook::domain::ChatDeleted>(
        [s](const rook::domain::ChatDeleted &event) { s->onChatDeleted(event); });
    s->m_updated_handler = bus.subscribe<rook::domain::ChatUpdated>(
        [s](const rook::domain::ChatUpdated &event) { s->onChatUpdated(event); });
    s->m_selected_handler = bus.subscribe<rook::domain::ChatSelected>(
        [s](const rook::domain::ChatSelected &event) { s->onChatSelected(event); });
    s->m_pinned_handler = bus.subscribe<rook::domain::ChatPinned>(
        [s](const rook::domain::ChatPinned &event) { s->onChatPinned(event); });
    s->m_snapshot_handler = bus.subscribe<rook::domain::SnapshotReady>(
        [s](const rook::domain::SnapshotReady &event) { s->onSnapshot(event); });

    return sidebar;
}

// --- Helpers ---

static Gtk::Stack* stackForRow(Gtk::ListBoxRow *row)
{
    return static_cast<Gtk::Stack*>(row->get_data("chat-stack"));
}

static std::string rowTitle(Gtk::ListBoxRow *row)
{
    auto *stack = stackForRow(row);
    if (!stack) return {};
    auto *label =
        stack->get_child_by_name("label_")->template cast<Gtk::Label>();
    if (!label) return {};
    auto t = label->get_text();
    return t ? std::string(t) : std::string{};
}

// --- Event handlers ---

void ChatSidebar::onNewChat(Gtk::Button *)
{
    if (m_actor)
        m_actor->post(rook::domain::ActorSelectChat{.chat_id = ""});
}

void ChatSidebar::onRowActivated(Gtk::ListBox *, Gtk::ListBoxRow *row)
{
    if (!row) return;
    auto chat_id = std::string(row->get_name());
    if (chat_id.empty()) return;
    if (m_actor)
        m_actor->post(rook::domain::ActorSelectChat{.chat_id = chat_id});
}

void ChatSidebar::onSearchChanged()
{
    auto query = m_search->get_text();
    auto raw = std::string(query ? query : "");
    m_no_results_placeholder->set_visible(false);

    bool any_visible = false;
    for (int i = 0; auto *row = m_list->get_row_at_index(i); ++i) {
        auto name = std::string(row->get_name());
        if (name == "__sep__") continue;

        bool match = raw.empty();
        if (!match) {
            auto *stack = stackForRow(row);
            if (stack) {
                auto *label = stack->get_child_by_name("label_")
                                  ->template cast<Gtk::Label>();
                if (label) {
                    auto title = std::string(label->get_text());
                    auto lt = title, lq = raw;
                    for (auto &c : lt) c = std::tolower(c);
                    for (auto &c : lq) c = std::tolower(c);
                    match = lt.find(lq) != std::string::npos;
                }
            }
        }

        row->set_visible(match);
        if (match) any_visible = true;
    }

    if (!raw.empty() && !any_visible) {
        m_no_results_placeholder->set_visible(true);
    }
}

// --- Build ---

Gtk::ListBoxRow* ChatSidebar::buildChatRow(std::string_view id,
                                            std::string title, bool pinned)
{
    auto display = title;
    if (display.size() > 30) display = display.substr(0, 30) + "...";

    std::string cid(id);

    auto row = Gtk::ListBoxRow::create();
    row->set_name(cid.c_str());

    auto outer = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 0);

    auto pin_icon = Gtk::Image::create_from_icon_name("view-pin-symbolic");
    pin_icon->set_visible(pinned);
    pin_icon->set_margin_start(6);
    pin_icon->set_margin_end(2);
    outer->append(std::move(pin_icon));

    auto stack = Gtk::Stack::create();
    Gtk::Stack* stack_ptr = stack;

    auto lbl = Gtk::Label::create(display.c_str());
    lbl->set_xalign(0.0f);
    lbl->set_margin_start(2);
    lbl->set_margin_end(6);
    lbl->set_margin_top(6);
    lbl->set_margin_bottom(6);
    lbl->set_max_width_chars(20);
    lbl->set_ellipsize(
        static_cast<Pango::EllipsizeMode>(PANGO_ELLIPSIZE_END));
    stack->add_named(std::move(lbl), "label_");

    auto ent = Gtk::Entry::create();
    ent->set_text(display.c_str());
    ent->set_margin_start(2);
    ent->set_margin_end(6);
    ent->set_margin_top(4);
    ent->set_margin_bottom(4);
    ent->set_hexpand(true);
    ent->connect_activate([this, cid](Gtk::Entry *e) {
        auto t = e->get_text();
        confirmRename(cid, t ? std::string(t) : std::string{});
    });
    stack->add_named(std::move(ent), "edit");

    stack->set_visible_child_name("label_");
    outer->append(std::move(stack));
    row->set_child(std::move(outer).release_floating_ptr());

    auto *row_ptr = static_cast<Gtk::ListBoxRow*>(row);
    row_ptr->set_data("chat-stack", stack_ptr);

    auto *ctx = new std::pair<ChatSidebar*, std::string>(this, cid);
    row_ptr->set_data("sidebar-ctx", ctx,
        [](void *p) {
            delete static_cast<std::pair<ChatSidebar*, std::string>*>(p);
        });

    auto ctrl = Gtk::GestureClick::create();
    ctrl->set_button(GDK_BUTTON_SECONDARY);
    ctrl->connect_pressed([this, row_ptr](Gtk::GestureClick *, int, double, double) {
        auto *pair = static_cast<std::pair<ChatSidebar*, std::string>*>(
            row_ptr->get_data("sidebar-ctx"));
        if (pair) pair->first->showContextMenu(row_ptr, pair->second);
    });
    row_ptr->add_controller(ctrl);

    return std::move(row).release_floating_ptr();
}

// --- Context menu ---


void ChatSidebar::showContextMenu(Gtk::Widget *,
                                   std::string_view chat_id)
{
    auto it = std::find_if(m_snapshot.conversations.begin(),
        m_snapshot.conversations.end(),
        [&](auto& c) { return c.id == chat_id; });
    bool is_pinned = it != m_snapshot.conversations.end() && it->pinned;
    std::string cid(chat_id);

    auto popover = Gtk::Popover::create();
    popover->set_has_arrow(false);

    gtk_widget_set_parent(
        GTK_WIDGET(reinterpret_cast<::GObject*>(
            static_cast<Gtk::Popover*>(popover))),
        GTK_WIDGET(reinterpret_cast<::GObject*>(m_list)));

    Graphene::Rect bounds;
    bool ok = m_list->compute_bounds(m_list, &bounds);
    Gdk::Rectangle rect = {
        ok ? static_cast<int>(bounds.origin.x) : 0,
        ok ? static_cast<int>(bounds.origin.y) : 0,
        ok ? static_cast<int>(bounds.size.width) : 1,
        ok ? static_cast<int>(bounds.size.height) : 1,
    };
    popover->set_pointing_to(&rect);
    auto *pop_ptr = static_cast<Gtk::Popover*>(popover);

    auto box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 0);

    auto add_btn = [&](const char *label, const char *icon,
                        std::function<void()> cb) {
        auto btn = Gtk::Button::create();
        btn->set_has_frame(false);
        auto inner = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
        inner->append(Gtk::Image::create_from_icon_name(icon));
        inner->append(Gtk::Label::create(label));
        btn->set_child(std::move(inner).release_floating_ptr());
        btn->connect_clicked(
            [cb = std::move(cb)](Gtk::Button *) { cb(); });
        box->append(std::move(btn));
    };

    add_btn(is_pinned ? _("Unpin") : _("Pin"), "view-pin-symbolic",
            [this, cid, pop_ptr]() {
                if (m_actor)
                    m_actor->post(rook::domain::ActorTogglePin{.chat_id = cid});
                pop_ptr->popdown();
            });

    add_btn(_("Rename"), "document-edit-symbolic",
            [this, cid, pop_ptr]() {
                startRename(cid);
                pop_ptr->popdown();
            });

    auto del_btn = Gtk::Button::create();
    del_btn->set_has_frame(false);
    {
        auto inner = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
        inner->append(
            Gtk::Image::create_from_icon_name("user-trash-symbolic"));
        auto del_label = Gtk::Label::create(_("Delete"));
        del_label->add_css_class("error");
        inner->append(std::move(del_label));
        del_btn->set_child(std::move(inner).release_floating_ptr());
    }
    del_btn->connect_clicked([this, cid, pop_ptr](Gtk::Button *) {
        confirmDelete(cid);
        pop_ptr->popdown();
    });
    box->append(std::move(del_btn));

    popover->set_child(std::move(box).release_floating_ptr());
    popover->popup();
}

// --- Rename ---

void ChatSidebar::startRename(std::string_view chat_id)
{
    for (int i = 0; auto *row = m_list->get_row_at_index(i); ++i) {
        if (std::string(row->get_name()) != chat_id) continue;
        m_rename_row = row;
        auto *stack = stackForRow(row);
        if (!stack) return;
        auto *entry_w =
            stack->get_child_by_name("edit")->template cast<Gtk::Entry>();
        if (!entry_w) return;
        auto current = rowTitle(row);
        entry_w->set_text(current.c_str());
        entry_w->set_position(-1);
        stack->set_visible_child_name("edit");
        entry_w->grab_focus();
        return;
    }
    m_rename_row = nullptr;
}

void ChatSidebar::confirmRename(std::string_view chat_id,
                                 std::string_view new_title)
{
    auto trimmed = std::string(new_title);
    auto start = trimmed.find_first_not_of(" \t\n\r");
    auto end = trimmed.find_last_not_of(" \t\n\r");
    if (start == std::string::npos || trimmed.empty()) {
        cancelRename();
        return;
    }
    trimmed = trimmed.substr(start, end - start + 1);
    if (trimmed.empty()) {
        cancelRename();
        return;
    }
    if (m_actor)
        m_actor->post(rook::domain::ActorRenameChat{
            .chat_id = std::string(chat_id),
            .title = std::string(trimmed)});
    cancelRename();
}

void ChatSidebar::cancelRename()
{
    if (!m_rename_row) return;
    auto *stack = stackForRow(m_rename_row);
    if (stack) stack->set_visible_child_name("label_");
    m_rename_row = nullptr;
}

// --- Delete ---

void ChatSidebar::confirmDelete(std::string_view chat_id)
{
    auto dialog = Adw::MessageDialog::create(
        nullptr, _("Delete Conversation"),
        _("This action cannot be undone."));
    dialog->add_response("cancel", _("Cancel"));
    dialog->add_response("delete", _("Delete"));
    dialog->set_response_appearance("delete",
        Adw::ResponseAppearance::DESTRUCTIVE);
    dialog->set_close_response("cancel");

    ChatSidebar *sidebar = this;
    std::string cid2(chat_id);
    dialog->connect_response(
        [sidebar, cid2](Adw::MessageDialog *, const char *response) {
            if (std::string(response) == "delete") {
                if (sidebar->m_actor)
                    sidebar->m_actor->post(
                        rook::domain::ActorDeleteChat{.chat_id = cid2});
            }
        });
    dialog->present();
}

// --- Event subscriptions ---

void ChatSidebar::onChatCreated(const rook::domain::ChatCreated &event)
{
    if (event.chat_id.empty()) return;
    GLib::idle_add_once([this, id = event.chat_id]() {
        rebuildList();
        if (m_search) m_search->set_text("");
        onSearchChanged();
    });
}

void ChatSidebar::onChatDeleted(const rook::domain::ChatDeleted &event)
{
    GLib::idle_add_once([this, id = std::string(event.chat_id)]() {
        rebuildList();
        if (m_search) m_search->set_text("");
        onSearchChanged();
    });
}

void ChatSidebar::onChatUpdated(const rook::domain::ChatUpdated &event)
{
    GLib::idle_add_once(
        [this, id = event.chat_id, title = event.title]() {
            for (int i = 0; auto *row = m_list->get_row_at_index(i); ++i) {
                if (std::string(row->get_name()) != id) continue;
                auto *stack = stackForRow(row);
                if (!stack) return;
                auto display = title;
                if (display.size() > 30)
                    display = display.substr(0, 30) + "...";

                auto *label = stack->get_child_by_name("label_")
                                  ->template cast<Gtk::Label>();
                if (label) label->set_text(display.c_str());

                auto *entry = stack->get_child_by_name("edit")
                                  ->template cast<Gtk::Entry>();
                if (entry) entry->set_text(display.c_str());
                return;
            }
        });
}

void ChatSidebar::onChatSelected(const rook::domain::ChatSelected &event)
{
    GLib::idle_add_once([this, id = event.chat_id]() {
        if (id.empty()) {
            m_list->select_row(nullptr);
            return;
        }
        for (int i = 0; auto *row = m_list->get_row_at_index(i); ++i) {
            if (std::string(row->get_name()) == id) {
                m_list->select_row(row);
                return;
            }
        }
    });
}

void ChatSidebar::onChatPinned(const rook::domain::ChatPinned &event)
{
    GLib::idle_add_once([this, id = event.chat_id]() {
        rebuildList();
        if (m_search) { m_search->set_text(""); onSearchChanged(); }
    });
}

void ChatSidebar::onSnapshot(const rook::domain::SnapshotReady &event)
{
    GLib::idle_add_once([this, snap = event]() {
        m_snapshot = snap;
        rebuildList();
        if (m_search) { m_search->set_text(""); onSearchChanged(); }
        if (!snap.active_chat_id.empty()) {
            for (int i = 0; auto *row = m_list->get_row_at_index(i); ++i) {
                if (std::string(row->get_name()) == snap.active_chat_id) {
                    m_list->select_row(row);
                    return;
                }
            }
        }
    });
}

// --- Rebuild ---

void ChatSidebar::rebuildList()
{
    while (auto *row = m_list->get_row_at_index(0))
        m_list->remove(row);

    m_rename_row = nullptr;

    auto chats = m_snapshot.conversations;

    std::sort(chats.begin(), chats.end(),
        [](const rook::domain::SnapshotConversation &a,
           const rook::domain::SnapshotConversation &b) {
            if (a.pinned != b.pinned) return a.pinned > b.pinned;
            if (a.pinned && b.pinned)
                return a.pinned_at > b.pinned_at;
            return a.updated_at > b.updated_at;
        });

    bool seen_unpinned = false;
    bool has_pinned = false;
    bool has_unpinned = false;

    for (auto it = chats.begin(); it != chats.end(); ++it) {
        if (it->pinned) has_pinned = true;
        else has_unpinned = true;

        if (!it->pinned && !seen_unpinned && has_pinned) {
            auto sep_row = Gtk::ListBoxRow::create();
            sep_row->set_name("__sep__");
            sep_row->set_sensitive(false);
            sep_row->set_activatable(false);
            sep_row->set_selectable(false);
            sep_row->add_css_class("separator-row");
            auto sep_widget = Gtk::Separator::create(
                Gtk::Orientation::HORIZONTAL);
            sep_widget->set_margin_top(2);
            sep_widget->set_margin_bottom(2);
            sep_row->set_child(
                std::move(sep_widget).release_floating_ptr());
            sep_row->set_visible(false);
            m_list->append(
                std::move(sep_row).release_floating_ptr());
            seen_unpinned = true;
        }

        auto display = it->title.empty() ? it->id : it->title;
        auto *row = buildChatRow(it->id, display, it->pinned);
        m_list->append(row);
    }

    m_empty_placeholder->set_visible(chats.empty());

    if (has_pinned && has_unpinned) {
        for (int i = 0; auto *r = m_list->get_row_at_index(i); ++i) {
            if (std::string(r->get_name()) == "__sep__") {
                r->set_visible(true);
                break;
            }
        }
    }
}

void ChatSidebar::loadConversations(
    const std::vector<rook::domain::Conversation> & /*chats*/)
{
    rebuildList();
    if (m_search) m_search->set_text("");
}

} // namespace rook::gui
