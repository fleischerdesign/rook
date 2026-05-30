#include "chat_view.hpp"
#include "message_widget.hpp"
#include "rook/domain/events.hpp"
#include <spdlog/spdlog.h>

namespace rook::gui {

ChatView::ChatView(rook::domain::EventBus& bus, rook::domain::ConversationManager& conv)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 6)
    , m_bus(bus)
    , m_conv(conv)
{
    setupUi();

    m_chunk_handler = m_bus.subscribe<rook::domain::LlmStreamChunk>(
        [this](const rook::domain::LlmStreamChunk& event) {
            onStreamChunk(event);
        });

    m_completed_handler = m_bus.subscribe<rook::domain::LlmCompleted>(
        [this](const rook::domain::LlmCompleted& event) {
            onLlmCompleted(event);
        });

    m_chat_selected_handler = m_bus.subscribe<rook::domain::ChatSelected>(
        [this](const rook::domain::ChatSelected& event) {
            onChatSelected(event);
        });
}

ChatView::~ChatView() {
    m_bus.unsubscribe(m_chunk_handler);
    m_bus.unsubscribe(m_completed_handler);
    m_bus.unsubscribe(m_chat_selected_handler);
}

void ChatView::setChatId(std::string_view id) {
    m_chat_id = id;
}

void ChatView::setupUi() {
    m_message_list.set_hexpand(true);
    m_message_list.set_vexpand(true);

    m_scrolled.set_child(m_message_list);
    m_scrolled.set_vexpand(true);
    append(m_scrolled);

    auto input_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    input_box->set_margin(12);

    m_input_entry.set_hexpand(true);
    m_input_entry.set_placeholder_text("Type a message...");
    m_input_entry.signal_activate().connect(
        sigc::mem_fun(*this, &ChatView::onMessageEntryActivated));
    input_box->append(m_input_entry);

    m_send_button.set_label("Send");
    m_send_button.add_css_class("suggested-action");
    m_send_button.signal_clicked().connect(
        sigc::mem_fun(*this, &ChatView::onSendClicked));
    input_box->append(m_send_button);

    append(*input_box);
}

void ChatView::onSendClicked() {
    auto text = m_input_entry.get_text();
    if (text.empty() || m_chat_id.empty()) return;

    m_input_entry.set_text("");

    auto* user_msg = Gtk::make_managed<MessageWidget>("user", std::string(text));
    m_message_list.append(*user_msg);

    m_bus.publish(rook::domain::UserInputReceived{
        .chat_id = m_chat_id,
        .content = text,
        .source = "text",
    });
}

void ChatView::onMessageEntryActivated() {
    onSendClicked();
}

void ChatView::onStreamChunk(const rook::domain::LlmStreamChunk& event) {
    if (event.chat_id != m_chat_id) return;

    Glib::signal_idle().connect_once([this, content = std::string(event.content)]() {
        if (!m_pending_assistant) {
            m_pending_assistant = Gtk::make_managed<MessageWidget>(
                "assistant", "");
            m_message_list.append(*m_pending_assistant);
        }

        if (!content.empty()) {
            m_pending_assistant->appendChunk(content);
        }

        auto adj = m_scrolled.get_vadjustment();
        if (adj) adj->set_value(adj->get_upper());
    });
}

void ChatView::onLlmCompleted(const rook::domain::LlmCompleted& /*event*/) {
    Glib::signal_idle().connect_once([this]() {
        m_pending_assistant = nullptr;
    });
}

void ChatView::onChatSelected(const rook::domain::ChatSelected& event) {
    Glib::signal_idle().connect_once([this, id = event.chat_id]() {
        m_chat_id = id;
        loadMessages(id);
    });
}

void ChatView::loadMessages(std::string_view chat_id) {
    while (auto* row = m_message_list.get_row_at_index(0)) {
        m_message_list.remove(*row);
    }
    m_pending_assistant = nullptr;

    auto conv = m_conv.open(chat_id);
    for (const auto& msg : conv.messages) {
        if (msg.role == "tool") continue;
        auto* widget = Gtk::make_managed<MessageWidget>(msg.role, msg.content);
        m_message_list.append(*widget);
    }

    auto adj = m_scrolled.get_vadjustment();
    if (adj) adj->set_value(adj->get_upper());
}

} // namespace rook::gui
