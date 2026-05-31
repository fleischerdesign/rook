#include "chat_view.hpp"
#include "message_widget.hpp"
#include "rook/adapters/model/model_cache.hpp"
#include "rook/ports/model_discovery_port.hpp"
#include <spdlog/spdlog.h>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(ChatView, "RookChatView", Gtk::Box)

inline void ChatView::Class::init()
{
    override_vfunc_dispose<ChatView>();
}

inline void ChatView::init(Class *)
{
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
        m_bus = nullptr;
    }
    parent_vfunc_dispose<ChatView>();
}

FloatPtr<ChatView> ChatView::create(rook::domain::EventBus &bus,
                                      rook::domain::ConversationManager &conv,
                                      rook::ports::LlmPort &llm)
{
    auto view = Object::create<ChatView>();
    ChatView *v = view;
    v->m_bus = &bus;
    v->m_conv = &conv;
    v->m_llm = &llm;

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

    auto stack = Gtk::Stack::create();
    stack->set_vexpand(true);

    auto welcome_page = Adw::StatusPage::create();
    welcome_page->set_title("Welcome to Rook");
    welcome_page->set_description(
        "Your multi-modal AI assistant. Ask anything, explore ideas.");
    welcome_page->add_css_class("compact");

    auto welcome_bar = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
    welcome_bar->set_margin_start(12);
    welcome_bar->set_margin_end(12);
    welcome_bar->set_margin_bottom(12);

    {
        const char *loading[] = {"Loading...", nullptr};
        auto model = Gtk::DropDown::create_from_strings(loading);
        model->set_margin_end(6);
        v->m_welcome_model = model;
        welcome_bar->append(std::move(model));
    }
    {
        auto entry = Gtk::Entry::create();
        entry->set_hexpand(true);
        entry->set_placeholder_text("Type a message...");
        entry->connect_activate([v](Gtk::Entry *) { v->onMessageEntryActivated(nullptr); });
        v->m_welcome_entry = entry;
        welcome_bar->append(std::move(entry));
    }
    {
        auto send = Gtk::Button::create_with_label("Send");
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
    msg_list->set_hexpand(true);
    msg_list->set_vexpand(true);
    v->m_message_list = msg_list;

    auto scrolled = Gtk::ScrolledWindow::create();
    scrolled->set_child(std::move(msg_list));
    scrolled->set_vexpand(true);
    v->m_scrolled = scrolled;
    chat_page->append(std::move(scrolled));

    auto chat_bar = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
    chat_bar->set_margin_start(12);
    chat_bar->set_margin_end(12);
    chat_bar->set_margin_bottom(12);

    {
        const char *loading[] = {"Loading...", nullptr};
        auto model = Gtk::DropDown::create_from_strings(loading);
        model->set_margin_end(6);
        v->m_chat_model = model;
        chat_bar->append(std::move(model));
    }
    {
        auto entry = Gtk::Entry::create();
        entry->set_hexpand(true);
        entry->set_placeholder_text("Type a message...");
        entry->connect_activate([v](Gtk::Entry *) { v->onMessageEntryActivated(nullptr); });
        v->m_chat_entry = entry;
        chat_bar->append(std::move(entry));
    }
    {
        auto send = Gtk::Button::create_with_label("Send");
        send->add_css_class("suggested-action");
        send->connect_clicked([v](Gtk::Button *) { v->onSendClicked(nullptr); });
        v->m_chat_send = send;
        chat_bar->append(std::move(send));
    }

    chat_page->append(std::move(chat_bar));

    v->m_stack = stack;

    stack->add_named(std::move(welcome_box), "welcome");
    stack->add_named(std::move(chat_page), "chat");
    stack->set_visible_child_name("welcome");

    v->append(std::move(stack));

    v->populateModelDropdown();

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
        m_bus->publish(rook::domain::ChatCreated{.chat_id = ""});
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

    std::string model_id;
    auto idx = m_chat_model->get_selected();
    if (idx < m_chat_ids.size()) model_id = m_chat_ids[idx];

    m_bus->publish(rook::domain::UserInputReceived{
        .chat_id = std::string(chat_id),
        .content = text,
        .source = "text",
        .model = model_id,
    });
}

void ChatView::onMessageEntryActivated(Gtk::Entry *)
{
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
        auto chats = m_conv->list();
        if (chats.empty()) {
            m_chat_id.clear();
            m_stack->set_visible_child_name("welcome");
        }
    });
}

void ChatView::switchToChat(std::string_view chat_id)
{
    m_chat_id = chat_id;
    m_stack->set_visible_child_name("chat");
    loadMessages(chat_id);

    if (!m_pending_input.empty()) {
        m_chat_entry->set_text(m_pending_input.c_str());
        auto text = m_pending_input;
        m_pending_input.clear();

        auto *user_msg = MessageWidget::create("user", text).release_floating_ptr();
        m_message_list->append(user_msg);

        std::string model_id;
        auto idx = m_chat_model->get_selected();
        if (idx < m_chat_ids.size()) model_id = m_chat_ids[idx];

        m_bus->publish(rook::domain::UserInputReceived{
            .chat_id = m_chat_id,
            .content = text,
            .source = "text",
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

    auto conv = m_conv->open(chat_id);
    for (const auto &msg : conv.messages) {
        if (msg.role == "tool") continue;
        auto *widget = MessageWidget::create(
            msg.role, msg.content, msg.reasoning_content)
            .release_floating_ptr();
        m_message_list->append(widget);
    }

    if (!conv.model.empty()) {
        for (size_t i = 0; i < m_chat_ids.size(); ++i) {
            if (m_chat_ids[i] == conv.model) {
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

} // namespace rook::gui
