#include "chat_view.hpp"
#include "message_widget.hpp"
#include "rook/domain/events.hpp"
#include "rook/ports/llm_port.hpp"
#include <spdlog/spdlog.h>

namespace rook::gui {

ChatView::ChatView(rook::domain::EventBus& bus, rook::domain::ConversationManager& conv,
                   rook::ports::LlmPort& llm)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 6)
    , m_bus(bus)
    , m_conv(conv)
    , m_llm(llm)
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

    m_locked_handler = m_bus.subscribe<rook::domain::LlmRequested>(
        [this](const rook::domain::LlmRequested& event) {
            if (event.chat_id == m_chat_id) setProcessing(true);
        });

    m_unlock_handler = m_bus.subscribe<rook::domain::LlmCompleted>(
        [this](const rook::domain::LlmCompleted& event) {
            if (event.chat_id == m_chat_id) setProcessing(false);
        });

    m_error_unlock_handler = m_bus.subscribe<rook::domain::LlmError>(
        [this](const rook::domain::LlmError& event) {
            if (event.chat_id == m_chat_id) setProcessing(false);
        });

    m_chat_selected_handler = m_bus.subscribe<rook::domain::ChatSelected>(
        [this](const rook::domain::ChatSelected& event) {
            onChatSelected(event);
        });
}

ChatView::~ChatView() {
    m_bus.unsubscribe(m_chunk_handler);
    m_bus.unsubscribe(m_completed_handler);
    m_bus.unsubscribe(m_locked_handler);
    m_bus.unsubscribe(m_unlock_handler);
    m_bus.unsubscribe(m_error_unlock_handler);
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

    m_model_dropdown.set_hexpand(false);
    m_model_dropdown.set_margin_start(12);
    m_model_dropdown.set_margin_end(6);
    populateModelDropdown();

    auto input_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    input_box->set_margin(12);

    input_box->append(m_model_dropdown);

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

void ChatView::populateModelDropdown() {
    m_model_dropdown.remove_all();
    auto providers = m_llm.listProviders();

    for (const auto& prov : providers) {
        if (!prov.enabled) continue;
        auto info = rook::ports::ProviderRegistry::instance().find(prov.type);
        auto models = info ? info->known_models : std::vector<std::string>{prov.default_model};
        if (models.empty()) models = {prov.default_model};

        for (const auto& model : models) {
            auto label = prov.display_name + " / " + model;
            m_model_dropdown.append(prov.id + ":" + model, label);
        }
    }

    if (m_model_dropdown.get_active_id().empty() && !providers.empty()) {
        auto& first = providers.front();
        auto info = rook::ports::ProviderRegistry::instance().find(first.type);
        auto default_model = info && !info->default_model.empty()
            ? info->default_model : first.default_model;
        m_model_dropdown.set_active_id(first.id + ":" + default_model);
    }

    m_model_dropdown.set_sensitive(true);
}

void ChatView::onSendClicked() {
    auto text = m_input_entry.get_text();
    if (text.empty() || m_chat_id.empty()) return;

    m_input_entry.set_text("");

    auto* user_msg = Gtk::make_managed<MessageWidget>("user", std::string(text));
    m_message_list.append(*user_msg);

    auto combo_id = std::string(m_model_dropdown.get_active_id());

    m_bus.publish(rook::domain::UserInputReceived{
        .chat_id = m_chat_id,
        .content = text,
        .source = "text",
        .model = combo_id,
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

void ChatView::setProcessing(bool active) {
    Glib::signal_idle().connect_once([this, active]() {
        m_send_button.set_sensitive(!active);
        m_input_entry.set_sensitive(!active);
        if (active) {
            m_send_button.set_label("...");
        } else {
            m_send_button.set_label("Send");
        }
    });
}

} // namespace rook::gui
