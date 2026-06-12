#include <glib/gi18n.h>
#include "chat_view.hpp"
#include "message_widget.hpp"
#include "tool_call_row.hpp"
#include "markdown_renderer.hpp"
#include "permission_banner.hpp"
#include "rook/core/domain_actor.hpp"
#include "rook/core/actor_messages.hpp"
#include "rook/ports/extension_port.hpp"
#include "rook/adapters/model/model_cache.hpp"
#include <peel/GLib/functions.h>
#include <peel/Gtk/Orientable.h>
#include <spdlog/spdlog.h>
#include <gtk/gtk.h>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(ChatView, "RookChatView", Gtk::Box)

inline void ChatView::Class::init()
{
    override_vfunc_dispose<ChatView>();
}

inline void ChatView::init(Class *)
{
    reinterpret_cast<Gtk::Orientable*>(this)->set_orientation(Gtk::Orientation::VERTICAL);
    new (&m_chat_id) std::string();
    new (&m_pending_input) std::string();
    new (&m_snapshot) rook::domain::SnapshotReady();
}

inline void ChatView::vfunc_dispose()
{
    if (m_bus) {
        m_bus->unsubscribe(m_chunk_handler);
        m_bus->unsubscribe(m_completed_handler);
        m_bus->unsubscribe(m_locked_handler);
        m_bus->unsubscribe(m_unlock_handler);
        m_bus->unsubscribe(m_error_unlock_handler);
        m_bus->unsubscribe(m_chat_selected_handler);
        m_bus->unsubscribe(m_chat_deleted_handler);
        m_bus->unsubscribe(m_tool_requested_handler);
        m_bus->unsubscribe(m_tool_completed_handler);
        m_bus->unsubscribe(m_skill_handler);
        m_bus->unsubscribe(m_perm_request_handler);
        m_bus->unsubscribe(m_perm_timeout_handler);
        m_bus->unsubscribe(m_snapshot_handler);
        m_bus->unsubscribe(m_voice_input_handler);
        m_bus = nullptr;
    }
    m_snapshot.~SnapshotReady();
    parent_vfunc_dispose<ChatView>();
}

FloatPtr<ChatView> ChatView::create(rook::core::DomainActor *actor,
                                       rook::domain::EventBus &bus,
                                       rook::ports::LlmPort &llm,
                                       rook::ports::ExtensionPort *extensions,
                                       std::vector<rook::adapters::extension::CustomSkill> *custom_skills,
                                       rook::ports::ToolPermissionPort *permission_port)
{
    auto view = Object::create<ChatView>();
    ChatView *v = view;
    v->m_actor = actor;
    v->m_bus = &bus;
    v->m_llm = &llm;
    v->m_extensions = extensions;
    v->m_custom_skills = custom_skills;
    v->m_permission_port = permission_port;

    v->m_chunk_handler = bus.subscribe<rook::domain::LlmStreamChunk>(
        [v](const rook::domain::LlmStreamChunk &event) {
            v->onStreamChunk(event);
        });
    v->m_completed_handler = bus.subscribe<rook::domain::LlmCompleted>(
        [v](const rook::domain::LlmCompleted &event) {
            v->onLlmCompleted(event);
        });
    v->m_locked_handler = bus.subscribe<rook::domain::LlmRequested>(
        [v](const rook::domain::LlmRequested &event) {
            if (event.chat_id == v->m_chat_id) v->setProcessing(true);
        });
    v->m_unlock_handler = bus.subscribe<rook::domain::LlmCompleted>(
        [v](const rook::domain::LlmCompleted &event) {
            if (event.chat_id == v->m_chat_id) v->setProcessing(false);
        });
    v->m_error_unlock_handler = bus.subscribe<rook::domain::LlmError>(
        [v](const rook::domain::LlmError &event) {
            if (event.chat_id == v->m_chat_id) v->setProcessing(false);
        });
    v->m_chat_selected_handler = bus.subscribe<rook::domain::ChatSelected>(
        [v](const rook::domain::ChatSelected &event) {
            v->onChatSelected(event);
        });
    v->m_chat_deleted_handler = bus.subscribe<rook::domain::ChatDeleted>(
        [v](const rook::domain::ChatDeleted &event) {
            v->onChatDeleted(event);
        });

    v->m_tool_requested_handler = bus.subscribe<rook::domain::ToolCallRequested>(
        [v](const rook::domain::ToolCallRequested &event) {
            v->onToolCallRequested(event);
        });
    v->m_tool_completed_handler = bus.subscribe<rook::domain::ToolCallCompleted>(
        [v](const rook::domain::ToolCallCompleted &event) {
            v->onToolCallCompleted(event);
        });

    v->m_skill_handler = bus.subscribe<rook::domain::SkillToggled>(
        [v](const rook::domain::SkillToggled& event) {
            if (event.chat_id == v->m_chat_id) {
                GLib::idle_add_once([v]() { v->buildSkillsPopover(); });
            }
        });

    v->m_perm_request_handler = bus.subscribe<rook::domain::ToolCallPermissionRequest>(
        [v](const rook::domain::ToolCallPermissionRequest& event) {
            GLib::idle_add_once([v, event]() { v->onPermissionRequest(event); });
        });

    v->m_perm_timeout_handler = bus.subscribe<rook::domain::ToolCallTimedOut>(
        [v](const rook::domain::ToolCallTimedOut& event) {
            GLib::idle_add_once([v, event]() { v->onPermissionTimeout(event); });
        });

    v->m_snapshot_handler = bus.subscribe<rook::domain::SnapshotReady>(
        [v](const rook::domain::SnapshotReady& event) {
            GLib::idle_add_once([v, snap = event]() { v->onSnapshot(snap); });
        });

    v->m_voice_input_handler = bus.subscribe<rook::domain::UserInputReceived>(
        [v](const rook::domain::UserInputReceived& event) {
            if (event.source == "voice_live" || event.source == "voice_wake") {
                GLib::idle_add_once([v, content = event.content, chat_id = event.chat_id]() {
                    if (v->m_chat_id == chat_id) {
                        auto* msg = MessageWidget::create("user", content)
                            .release_floating_ptr();
                        v->m_message_list->append(msg);
                        if (auto* parent = msg->get_parent()) {
                            if (auto* row = parent->template cast<Gtk::ListBoxRow>())
                                row->set_activatable(false);
                        }
                    }
                });
            }
        });

    auto stack = Gtk::Stack::create();
    stack->set_vexpand(true);

    auto welcome_page = Adw::StatusPage::create();
    welcome_page->set_title(_("Welcome to Rook"));
    welcome_page->set_description(
        _("Your multi-modal AI assistant. Ask anything, explore ideas."));
    welcome_page->add_css_class("compact");

    auto welcome_bar = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
    welcome_bar->set_margin_start(12);
    welcome_bar->set_margin_end(12);
    welcome_bar->set_margin_bottom(12);

    {
        auto skills_btn = Gtk::MenuButton::create();
        skills_btn->set_tooltip_text(_("Skills"));
        skills_btn->set_icon_name("applications-engineering-symbolic");
        skills_btn->set_margin_end(4);
        v->m_welcome_skills_btn = skills_btn;
        welcome_bar->append(std::move(skills_btn));
    }
    {
        const char *loading[] = {_("Loading..."), nullptr};
        auto model = Gtk::DropDown::create_from_strings(loading);
        model->set_margin_end(6);
        v->m_welcome_model = model;
        welcome_bar->append(std::move(model));
    }
    {
        auto entry = Gtk::Entry::create();
        entry->set_hexpand(true);
        entry->set_placeholder_text(_("Type a message..."));
        entry->connect_activate([v](Gtk::Entry *) { v->onMessageEntryActivated(nullptr); });
        entry->connect_changed([v](Gtk::Editable *) { v->onChatEntryChanged(); });

        auto key_ctrl = Gtk::EventControllerKey::create();
        key_ctrl->set_im_context(nullptr);
        entry->add_controller(key_ctrl);
        key_ctrl->connect_key_pressed(
            [v](Gtk::EventControllerKey *, unsigned keyval, unsigned,
                Gdk::ModifierType) -> bool {
                if (!v->m_command_listbox) return GDK_EVENT_PROPAGATE;

                if (keyval == GDK_KEY_Down || keyval == GDK_KEY_Up) {
                    auto* listbox = v->m_command_listbox;
                    auto* sel = listbox->get_selected_row();
                    int idx = sel ? sel->get_index() : -1;
                    idx += (keyval == GDK_KEY_Down) ? 1 : -1;
                    auto* row = listbox->get_row_at_index(idx);
                    if (row) listbox->select_row(row);
                    return GDK_EVENT_STOP;
                }

                if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
                    auto* listbox = v->m_command_listbox;
                    auto* sel = listbox->get_selected_row();
                    if (sel) {
                        if (ADW_IS_ACTION_ROW(sel))
                            adw_action_row_activate(ADW_ACTION_ROW(sel));
                    }
                    return GDK_EVENT_STOP;
                }

                return GDK_EVENT_PROPAGATE;
            });

        v->m_welcome_entry = entry;
        welcome_bar->append(std::move(entry));
    }
    {
        auto send = Gtk::Button::create_with_label(_("Send"));
        send->add_css_class("suggested-action");
        send->connect_clicked([v](Gtk::Button *) { v->onSendClicked(nullptr); });
        v->m_welcome_send = send;
        welcome_bar->append(std::move(send));
    }

    auto welcome_box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 12);
    welcome_box->set_valign(Gtk::Align::CENTER);
    welcome_box->set_halign(Gtk::Align::CENTER);
    welcome_box->set_vexpand(true);
    welcome_box->append(std::move(welcome_page));
    welcome_box->append(std::move(welcome_bar));

    auto chat_page = Gtk::Box::create(Gtk::Orientation::VERTICAL, 0);

    auto msg_list = Gtk::ListBox::create();
    msg_list->set_selection_mode(Gtk::SelectionMode::NONE);
    msg_list->set_hexpand(true);
    msg_list->set_vexpand(true);
    v->m_message_list = msg_list;

    auto scrolled = Gtk::ScrolledWindow::create();
    scrolled->set_child(std::move(msg_list));
    scrolled->set_vexpand(true);
    v->m_scrolled = scrolled;
    chat_page->append(std::move(scrolled));

    auto banner_slot = Gtk::Box::create(Gtk::Orientation::VERTICAL, 0);
    v->m_banner_slot = banner_slot;
    chat_page->append(std::move(banner_slot));

    auto chat_bar = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
    chat_bar->set_margin_start(12);
    chat_bar->set_margin_end(12);
    chat_bar->set_margin_bottom(12);

    {
        auto skills_btn = Gtk::MenuButton::create();
        skills_btn->set_tooltip_text(_("Skills"));
        skills_btn->set_icon_name("applications-engineering-symbolic");
        skills_btn->set_margin_end(4);
        v->m_skills_btn = skills_btn;
        chat_bar->append(std::move(skills_btn));
    }
    {
        const char *loading[] = {_("Loading..."), nullptr};
        auto model = Gtk::DropDown::create_from_strings(loading);
        model->set_margin_end(6);
        v->m_chat_model = model;
        chat_bar->append(std::move(model));
    }
    {
        auto entry = Gtk::Entry::create();
        entry->set_hexpand(true);
        entry->set_placeholder_text(_("Type a message..."));
        entry->connect_activate([v](Gtk::Entry *) { v->onMessageEntryActivated(nullptr); });
        entry->connect_changed([v](Gtk::Editable *) { v->onChatEntryChanged(); });

        auto key_ctrl2 = Gtk::EventControllerKey::create();
        key_ctrl2->set_im_context(nullptr);
        entry->add_controller(key_ctrl2);
        key_ctrl2->connect_key_pressed(
            [v](Gtk::EventControllerKey *, unsigned keyval, unsigned,
                Gdk::ModifierType) -> bool {
                if (!v->m_command_listbox) return GDK_EVENT_PROPAGATE;

                if (keyval == GDK_KEY_Down || keyval == GDK_KEY_Up) {
                    auto* listbox = v->m_command_listbox;
                    auto* sel = listbox->get_selected_row();
                    int idx = sel ? sel->get_index() : -1;
                    idx += (keyval == GDK_KEY_Down) ? 1 : -1;
                    auto* row = listbox->get_row_at_index(idx);
                    if (row) listbox->select_row(row);
                    return GDK_EVENT_STOP;
                }

                if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
                    auto* listbox = v->m_command_listbox;
                    auto* sel = listbox->get_selected_row();
                    if (sel) {
                        if (ADW_IS_ACTION_ROW(sel))
                            adw_action_row_activate(ADW_ACTION_ROW(sel));
                    }
                    return GDK_EVENT_STOP;
                }

                return GDK_EVENT_PROPAGATE;
            });

        v->m_chat_entry = entry;
        chat_bar->append(std::move(entry));
    }
    {
        auto send = Gtk::Button::create_with_label(_("Send"));
        send->add_css_class("suggested-action");
        send->connect_clicked([v](Gtk::Button *) { v->onSendClicked(nullptr); });
        v->m_chat_send = send;
        chat_bar->append(std::move(send));
    }
    {
        auto mic = Gtk::ToggleButton::create();
        mic->set_icon_name("microphone-symbolic");
        mic->set_tooltip_text(_("Voice Chat"));
        mic->connect_toggled([v](Gtk::ToggleButton* btn) {
            if (!v->m_chat_id.empty()) {
                auto actor = v->m_actor;
                auto cid = v->m_chat_id;
                GLib::idle_add_once([actor, cid, active = btn->get_active()]() {
                    actor->post(rook::domain::ActorVoiceLiveToggle{
                        .chat_id = cid, .enabled = active});
                });
            }
        });
        v->m_chat_mic = mic;
        chat_bar->append(std::move(mic));
    }

    chat_page->append(std::move(chat_bar));

    v->m_stack = stack;

    stack->add_named(std::move(welcome_box), "welcome");
    stack->add_named(std::move(chat_page), "chat");
    stack->set_visible_child_name("welcome");

    v->append(std::move(stack));

    v->populateModelDropdown();
    v->buildSkillsPopover();
    v->buildSkillsPopover(v->m_welcome_skills_btn);

    return view;
}

void ChatView::setChatId(std::string_view id)
{
    m_chat_id = id;
}

void ChatView::createInputBar(
    Gtk::DropDown *&model_out,
    Gtk::Entry *&entry_out,
    Gtk::Button *&button_out,
    std::vector<std::string> &ids_out)
{
    (void)model_out;
    (void)entry_out;
    (void)button_out;
    (void)ids_out;
}

void ChatView::populateModelDropdown()
{
    auto providers = m_llm->listProviders();

    m_welcome_ids.clear();
    m_chat_ids.clear();

    const char *empty_list[] = {nullptr};
    RefPtr<Gtk::StringList> welcome_strings = Gtk::StringList::create(empty_list);
    RefPtr<Gtk::StringList> chat_strings = Gtk::StringList::create(empty_list);

    for (const auto &prov : providers) {
        if (!prov.enabled) continue;

        auto cached_models = rook::adapters::model::ModelCache::instance().getAllEnabled(
            [&](std::string_view pid) { return pid == prov.id; });

        if (!cached_models.empty()) {
            for (const auto &model : cached_models) {
                auto id = prov.id + ":" + model.id;
                auto label = prov.display_name + " / " + model.display_name;
                m_welcome_ids.push_back(id);
                m_chat_ids.push_back(id);
                welcome_strings->append(label.c_str());
                chat_strings->append(label.c_str());
            }
        } else {
            auto info = rook::ports::ProviderRegistry::instance().find(prov.type);
            auto default_model = info ? info->default_model : prov.default_model;
            auto id = prov.id + ":" + default_model;
            auto label = prov.display_name + " / " + default_model;
            m_welcome_ids.push_back(id);
            m_chat_ids.push_back(id);
            welcome_strings->append(label.c_str());
            chat_strings->append(label.c_str());
        }
    }

    m_welcome_model->set_model(welcome_strings);
    m_chat_model->set_model(chat_strings);

    if (m_welcome_ids.empty()) return;

    m_welcome_model->set_selected(0);
    m_chat_model->set_selected(0);
}

void ChatView::onSendClicked(Gtk::Button *)
{
    if (m_stack->get_visible_child_name() == std::string("welcome")) {
        auto text = std::string(m_welcome_entry->get_text());
        if (text.empty()) return;

        m_welcome_entry->set_text("");
        m_pending_input = text;

        if (!m_chat_ids.empty() && !m_welcome_ids.empty()) {
            auto welcome_idx = m_welcome_model->get_selected();
            if (welcome_idx < m_welcome_ids.size()) {
                auto &welcome_id = m_welcome_ids[welcome_idx];
                for (size_t i = 0; i < m_chat_ids.size(); ++i) {
                    if (m_chat_ids[i] == welcome_id) {
                        m_chat_model->set_selected(i);
                        break;
                    }
                }
            }
        }

        setProcessing(true);
        if (m_actor)
            m_actor->post(rook::domain::ActorCreateChat{.title = "New Chat", .model = "default"});
        return;
    }

    doSend(m_chat_id);
}

void ChatView::doSend(std::string_view chat_id)
{
    auto text = std::string(m_chat_entry->get_text());
    if (text.empty()) return;

    m_chat_entry->set_text("");

    auto *user_msg = MessageWidget::create("user", text).release_floating_ptr();
    m_message_list->append(user_msg);
    if (auto* parent = user_msg->get_parent()) {
        if (auto *row = parent->template cast<Gtk::ListBoxRow>())
            row->set_activatable(false);
    }

    std::string model_id;
    auto idx = m_chat_model->get_selected();
    if (idx < m_chat_ids.size()) model_id = m_chat_ids[idx];

    if (m_actor)
        m_actor->post(rook::domain::ActorUserInput{
            .chat_id = std::string(chat_id),
            .content = text,
            .model = model_id,
        });
}

void ChatView::onMessageEntryActivated(Gtk::Entry *)
{
    if (m_command_popover && m_command_listbox && m_extensions
        && m_command_popover->is_visible()) {
        auto* sel = m_command_listbox->get_selected_row();
        if (sel) {
            if (ADW_IS_ACTION_ROW(sel))
                adw_action_row_activate(ADW_ACTION_ROW(sel));
            return;
        }
    }

    onSendClicked(nullptr);
}

void ChatView::onStreamChunk(const rook::domain::LlmStreamChunk &event)
{
    if (event.chat_id != m_chat_id) return;

    GLib::idle_add_once([this, content = std::string(event.content),
                          is_reasoning = event.is_reasoning]() {
        if (!m_pending_assistant) {
            auto msg = MessageWidget::create("assistant", "");
            m_pending_assistant = std::move(msg).release_floating_ptr();
            m_message_list->append(m_pending_assistant);
            if (auto* parent = m_pending_assistant->get_parent()) {
                if (auto *row = parent->template cast<Gtk::ListBoxRow>())
                    row->set_activatable(false);
            }
        }

        if (!content.empty()) {
            if (is_reasoning) {
                m_pending_assistant->appendReasoningChunk(content);
            } else {
                m_pending_assistant->appendChunk(content);
            }
        }

        auto adj = m_scrolled->get_vadjustment();
        if (adj) adj->set_value(adj->get_upper());
    });
}

void ChatView::onLlmCompleted(const rook::domain::LlmCompleted &)
{
    GLib::idle_add_once([this]() {
        m_pending_assistant = nullptr;
    });
}

void ChatView::onToolCallRequested(const rook::domain::ToolCallRequested &event)
{
    if (event.chat_id != m_chat_id) return;

    GLib::idle_add_once([this, name = event.tool_name,
                         call_id = event.call_id]() mutable {
        auto row = ToolCallRow::createPending(name, call_id);
        auto* ptr = std::move(row).release_floating_ptr();
        m_message_list->append(ptr);
        if (auto* parent = ptr->get_parent()) {
            if (auto *list_row = parent->template cast<Gtk::ListBoxRow>())
                list_row->set_activatable(false);
        }
        m_pending_tool_rows[call_id] = ptr;
    });
}

void ChatView::onToolCallCompleted(const rook::domain::ToolCallCompleted &event)
{
    if (event.chat_id != m_chat_id) return;

    GLib::idle_add_once([this, call_id = event.call_id,
                         result = event.result,
                         is_error = event.is_error]() {
        auto it = m_pending_tool_rows.find(call_id);
        if (it != m_pending_tool_rows.end()) {
            auto *row = static_cast<ToolCallRow*>(it->second);
            if (row) {
                row->setResult(result, is_error);
            }
        }
    });
}

void ChatView::onChatSelected(const rook::domain::ChatSelected &event)
{
    GLib::idle_add_once([this, id = event.chat_id]() {
        if (id.empty()) {
            m_chat_id.clear();
            m_stack->set_visible_child_name("welcome");
            return;
        }
        switchToChat(id);
    });
}

void ChatView::onChatDeleted(const rook::domain::ChatDeleted &)
{
    GLib::idle_add_once([this]() {
        if (m_snapshot.conversations.empty()) {
            m_chat_id.clear();
            m_stack->set_visible_child_name("welcome");
        }
    });
}

void ChatView::onSnapshot(const rook::domain::SnapshotReady& event)
{
    m_snapshot = event;
}

void ChatView::switchToChat(std::string_view chat_id)
{
    m_chat_id = chat_id;
    m_stack->set_visible_child_name("chat");
    loadMessages(chat_id);

    for (auto& sid : m_welcome_pending_skills) {
        if (m_actor)
            m_actor->post(rook::domain::ActorToggleSkill{
                .chat_id = m_chat_id,
                .skill_id = sid,
                .active = true,
            });
    }
    m_welcome_pending_skills.clear();

    buildSkillsPopover();

    if (!m_pending_input.empty()) {
        m_chat_entry->set_text(m_pending_input.c_str());
        auto text = m_pending_input;
        m_pending_input.clear();

        auto *user_msg = MessageWidget::create("user", text).release_floating_ptr();
        m_message_list->append(user_msg);
        if (auto* parent = user_msg->get_parent()) {
            if (auto *row = parent->template cast<Gtk::ListBoxRow>())
                row->set_activatable(false);
        }

        std::string model_id;
        auto idx = m_chat_model->get_selected();
        if (idx < m_chat_ids.size()) model_id = m_chat_ids[idx];

        if (m_actor)
            m_actor->post(rook::domain::ActorUserInput{
                .chat_id = m_chat_id,
                .content = text,
                .model = model_id,
            });
    }
}

void ChatView::loadMessages(std::string_view chat_id)
{
    while (auto *row = m_message_list->get_row_at_index(0)) {
        m_message_list->remove(row);
    }
    m_pending_assistant = nullptr;
    m_pending_tool_rows.clear();

    auto it = std::find_if(m_snapshot.conversations.begin(),
        m_snapshot.conversations.end(),
        [&](const auto& c) { return c.id == chat_id; });
    if (it == m_snapshot.conversations.end()) return;

    for (const auto &msg : it->messages) {
        if (msg.role == "system") continue;
        if (msg.role == "tool") {
            auto *row = ToolCallRow::createCompleted(
                msg.tool_name.empty() ? msg.tool_call_id : msg.tool_name,
                "", msg.content, false).release_floating_ptr();
            m_message_list->append(row);
            if (auto* parent = row->get_parent()) {
                if (auto *list_row = parent->template cast<Gtk::ListBoxRow>())
                    list_row->set_activatable(false);
            }
            continue;
        }
        auto *widget = MessageWidget::create(
            msg.role, msg.content, "")
            .release_floating_ptr();
        m_message_list->append(widget);
        if (auto* parent = widget->get_parent()) {
            if (auto *row = parent->template cast<Gtk::ListBoxRow>())
                row->set_activatable(false);
        }
    }

    if (!it->model.empty()) {
        for (size_t i = 0; i < m_chat_ids.size(); ++i) {
            if (m_chat_ids[i] == it->model) {
                m_chat_model->set_selected(i);
                break;
            }
        }
    }

    auto adj = m_scrolled->get_vadjustment();
    if (adj) adj->set_value(adj->get_upper());
}

void ChatView::setProcessing(bool active)
{
    GLib::idle_add_once([this, active]() {
        m_welcome_send->set_sensitive(!active);
        m_welcome_entry->set_sensitive(!active);
        m_chat_send->set_sensitive(!active);
        m_chat_entry->set_sensitive(!active);

        const char *label = active ? "..." : "Send";
        m_welcome_send->set_label(label);
        m_chat_send->set_label(label);
    });
}

void ChatView::buildSkillsPopover(Gtk::MenuButton *target)
{
    if (!target) target = m_skills_btn;
    if (!target) return;

    auto popover = Gtk::Popover::create();
    auto content = Gtk::Box::create(Gtk::Orientation::VERTICAL, 4);
    content->set_margin_start(12);
    content->set_margin_end(12);
    content->set_margin_top(8);
    content->set_margin_bottom(8);

    auto heading = Gtk::Label::create(_("Skills for this chat"));
    heading->set_xalign(0.0f);
    heading->set_use_markup(true);
    heading->set_markup(("<span weight=\"bold\">" + std::string(_("Skills for this chat")) + "</span>").c_str());
    content->append(std::move(heading));

    auto sep = Gtk::Separator::create(Gtk::Orientation::HORIZONTAL);
    content->append(std::move(sep));

    auto list = Gtk::ListBox::create();
    list->set_selection_mode(Gtk::SelectionMode::NONE);
    list->add_css_class("rich-list");

    std::vector<std::string> active_ids;
    if (!m_chat_id.empty()) {
        auto it = std::find_if(m_snapshot.conversations.begin(),
            m_snapshot.conversations.end(),
            [&](auto& c) { return c.id == m_chat_id; });
        if (it != m_snapshot.conversations.end())
            active_ids = it->active_skill_ids;
    }

    if (m_chat_id.empty()) {
        if (m_custom_skills) {
            for (auto& s : *m_custom_skills)
                if (s.enabled && !s.prompt.empty())
                    active_ids.push_back("custom:" + s.name);
        }
        if (m_extensions) {
            for (auto& ext : m_extensions->listInstalled())
                for (auto& s : ext.skills)
                    if (s.enabled && !s.prompt.empty())
                        active_ids.push_back("ext:" + ext.name + ":" + s.name);
        }
        for (auto& sid : m_welcome_pending_skills)
            if (std::find(active_ids.begin(), active_ids.end(), sid) == active_ids.end())
                active_ids.push_back(sid);
    }

    if (m_custom_skills) {
        for (auto& skill : *m_custom_skills) {
            if (skill.prompt.empty()) continue;

            auto row = Adw::ActionRow::create();
            row->set_title(skill.name.c_str());
            if (!skill.description.empty())
                row->set_subtitle(skill.description.c_str());

            std::string sid = "custom:" + skill.name;
            auto check = Gtk::CheckButton::create();
            check->set_active(std::find(active_ids.begin(), active_ids.end(), sid)
                           != active_ids.end());

            ChatView *raw_v = this;
            check->connect_toggled([raw_v, sid](Gtk::CheckButton *cb) {
                if (raw_v->m_chat_id.empty()) {
                    if (cb->get_active())
                        raw_v->m_welcome_pending_skills.push_back(sid);
                    else
                        std::erase(raw_v->m_welcome_pending_skills, sid);
                    return;
                }
                raw_v->m_actor->post(rook::domain::ActorToggleSkill{
                    .chat_id = raw_v->m_chat_id,
                    .skill_id = sid,
                    .active = cb->get_active(),
                });
            });

            row->add_prefix(std::move(check).release_floating_ptr());
            list->append(std::move(row).release_floating_ptr());
        }
    }

    if (m_extensions) {
        auto installed = m_extensions->listInstalled();
        for (auto& ext : installed) {
            for (auto& skill : ext.skills) {
                if (skill.prompt.empty()) continue;

                auto row = Adw::ActionRow::create();
                row->set_title(skill.name.c_str());
                std::string subtitle = std::string(_("via ")) + ext.display_name;
                row->set_subtitle(subtitle.c_str());

                std::string sid = "ext:" + ext.name + ":" + skill.name;

                auto check = Gtk::CheckButton::create();
                check->set_active(std::find(active_ids.begin(), active_ids.end(), sid)
                               != active_ids.end());

                ChatView *raw_v = this;
                check->connect_toggled([raw_v, sid](Gtk::CheckButton *cb) {
                    if (raw_v->m_chat_id.empty()) {
                        if (cb->get_active())
                            raw_v->m_welcome_pending_skills.push_back(sid);
                        else
                            std::erase(raw_v->m_welcome_pending_skills, sid);
                        return;
                    }
                    raw_v->m_actor->post(rook::domain::ActorToggleSkill{
                        .chat_id = raw_v->m_chat_id,
                        .skill_id = sid,
                        .active = cb->get_active(),
                    });
                });

                row->add_prefix(std::move(check).release_floating_ptr());
                list->append(std::move(row).release_floating_ptr());
            }
        }
    }

    content->append(std::move(list));
    popover->set_child(std::move(content).release_floating_ptr());

    m_skills_popover = popover;
    target->set_popover(std::move(popover));
}

void ChatView::onChatEntryChanged()
{
    auto* entry = m_stack->get_visible_child_name() == std::string("welcome")
        ? m_welcome_entry : m_chat_entry;
    if (!entry) return;

    auto text = std::string(entry->get_text());
    if (!text.starts_with("/")) {
        if (m_command_popover) {
            gtk_popover_popdown(GTK_POPOVER(
                m_command_popover.operator Gtk::Popover*()));
            m_command_popover = {};
            m_command_listbox = nullptr;
        }
        return;
    }

    auto prefix = text.substr(1);

    auto installed = m_extensions ? m_extensions->listInstalled()
        : std::vector<rook::adapters::extension::InstalledExtension>{};
    std::vector<std::pair<std::string, std::string>> matches;
    for (auto& ext : installed) {
        for (auto& cmd : ext.commands) {
            if (prefix.empty() || cmd.name.starts_with(prefix)) {
                matches.emplace_back(cmd.name,
                    cmd.description + " (" + ext.display_name + ")");
            }
        }
    }

    if (matches.empty()) {
        if (m_command_popover) {
            gtk_popover_popdown(GTK_POPOVER(
                m_command_popover.operator Gtk::Popover*()));
            m_command_popover = {};
            m_command_listbox = nullptr;
        }
        return;
    }

    if (m_command_popover) {
        m_command_popover->popdown();
        m_command_popover = {};
        m_command_listbox = nullptr;
    }

    m_command_popover = Gtk::Popover::create();
    gtk_widget_set_parent(
        GTK_WIDGET(m_command_popover.operator Gtk::Popover*()),
        GTK_WIDGET(entry));
    m_command_popover->set_autohide(false);
    m_command_popover->set_has_arrow(false);

    auto content = Gtk::Box::create(Gtk::Orientation::VERTICAL, 2);
    content->set_margin_start(4);
    content->set_margin_end(4);
    content->set_margin_top(4);
    content->set_margin_bottom(4);

    auto list = Gtk::ListBox::create();
    m_command_listbox = list;
    list->set_selection_mode(Gtk::SelectionMode::SINGLE);
    list->add_css_class("rich-list");
    content->append(std::move(list));

    m_command_popover->set_child(
        std::move(content).release_floating_ptr());

    ChatView* raw_v = this;
    for (auto& [name, desc] : matches) {
        auto row = Adw::ActionRow::create();
        row->set_title(("/" + name).c_str());
        row->set_subtitle(desc.c_str());
        std::string cmd_text = "/" + name + " ";
        row->connect_activated([raw_v, cmd_text](Adw::ActionRow*) {
            auto* entry = raw_v->m_stack->get_visible_child_name()
                == std::string("welcome")
                ? raw_v->m_welcome_entry : raw_v->m_chat_entry;
            if (!entry) return;
            entry->set_text(cmd_text.c_str());
            entry->set_position(-1);
            if (raw_v->m_command_popover)
                raw_v->m_command_popover->popdown();
        });

        auto* row_ptr = row.operator Adw::ActionRow*();
        auto gesture = Gtk::GestureClick::create();
        gesture->set_button(1);
        gesture->connect_released(
            [row_ptr](Gtk::GestureClick *, int, double, double) {
                adw_action_row_activate(ADW_ACTION_ROW(row_ptr));
            });
        row_ptr->add_controller(gesture);

        m_command_listbox->append(std::move(row).release_floating_ptr());
    }

    m_command_popover->set_position(
        entry == m_chat_entry ? Gtk::PositionType::TOP : Gtk::PositionType::BOTTOM);

    m_command_popover->popup();
}

void ChatView::onPermissionRequest(
    const rook::domain::ToolCallPermissionRequest& event)
{
    if (m_active_banner) {
        m_active_banner->unparent();
        m_active_banner = nullptr;
    }
    if (m_banner_timeout_id) {
        g_source_remove(m_banner_timeout_id);
        m_banner_timeout_id = 0;
    }

    auto banner = PermissionBanner::create(event);
    auto *b = static_cast<PermissionBanner*>(banner);
    m_active_banner = b;

    b->on_decision =
        [this](std::string_view uuid,
               std::vector<rook::domain::ToolCallPermissionDecision::Result>
                   results) {
            if (m_actor) {
                rook::domain::ActorPermissionDecision ad;
                ad.request_uuid = std::string(uuid);
                for (auto& r : results) {
                    ad.results.push_back({r.call_id, r.decision});
                }
                m_actor->post(std::move(ad));
            }

            if (m_active_banner) {
                m_active_banner->unparent();
                m_active_banner = nullptr;
            }
            if (m_banner_timeout_id) {
                g_source_remove(m_banner_timeout_id);
                m_banner_timeout_id = 0;
            }
        };

    m_banner_slot->append(std::move(banner).release_floating_ptr());

    m_banner_timeout_id = GLib::timeout_add_seconds(30,
        [this]() -> bool {
            if (m_active_banner) {
                auto uid = m_active_banner->requestUuid();
                if (m_actor)
                    m_actor->post(
                        rook::domain::ActorPermissionTimeout{.request_uuid = uid});
                m_active_banner->unparent();
                m_active_banner = nullptr;
            }
            m_banner_timeout_id = 0;
            return false;
        });
}

void ChatView::onPermissionTimeout(
    const rook::domain::ToolCallTimedOut& /*event*/)
{
    if (m_active_banner) {
        m_active_banner->unparent();
        m_active_banner = nullptr;
    }
    if (m_banner_timeout_id) {
        g_source_remove(m_banner_timeout_id);
        m_banner_timeout_id = 0;
    }
}

} // namespace rook::gui
