#include "message_widget.hpp"
#include <pango/pango.h>

namespace rook::gui {

MessageWidget::MessageWidget(std::string_view role, std::string_view content)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 4)
    , m_role(role)
{
    m_label.set_wrap(true);
    m_label.set_wrap_mode(Pango::WrapMode::WORD_CHAR);
    m_label.set_max_width_chars(60);
    m_label.set_selectable(true);
    m_label.set_use_markup(true);
    m_label.set_xalign(0.0f);

    setContent(content);
    applyStyle();

    append(m_label);
    set_margin(8);
    set_margin_start(12);
    set_margin_end(12);
}

void MessageWidget::appendChunk(std::string_view chunk) {
    auto text = m_label.get_text();
    text.append(std::string(chunk));
    m_label.set_text(text);
}

void MessageWidget::setContent(std::string_view content) {
    m_label.set_text(std::string(content));
}

void MessageWidget::applyStyle() {
    if (m_role == "user") {
        add_css_class("message-user");
        m_label.set_halign(Gtk::Align::END);
        m_label.add_css_class("frame");
    } else {
        add_css_class("message-assistant");
        m_label.set_halign(Gtk::Align::START);
    }
}

} // namespace rook::gui
