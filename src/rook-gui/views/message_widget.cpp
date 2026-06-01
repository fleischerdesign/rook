#include "message_widget.hpp"
#include <gtk/gtk.h>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(MessageWidget, "RookMessageWidget", Gtk::Box)

inline void MessageWidget::Class::init()
{
    set_css_name("messagewidget");
}

inline void MessageWidget::init(Class *)
{
    gtk_orientable_set_orientation(
        GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)),
        GTK_ORIENTATION_VERTICAL);
    set_spacing(2);
    set_margin_top(6);
    set_margin_bottom(6);
}

FloatPtr<MessageWidget> MessageWidget::create(const std::string &role,
                                                const std::string &content,
                                                const std::string &reasoning_content)
{
    auto widget = Object::create<MessageWidget>();
    widget->m_role = role;

    if (!reasoning_content.empty()) {
        auto expander = Gtk::Expander::create("Thinking...");
        expander->set_expanded(false);

        auto rlabel = Gtk::Label::create(reasoning_content.c_str());
        rlabel->set_wrap(true);
        rlabel->set_xalign(0.0f);
        rlabel->set_max_width_chars(80);
        rlabel->add_css_class("dim-label");
        rlabel->set_use_markup(true);

        Gtk::Label *rlabel_ptr = rlabel;
        Gtk::Expander *expander_ptr = expander;
        expander->set_child(std::move(rlabel).release_floating_ptr());
        widget->m_reasoning_expander = expander_ptr;
        widget->m_reasoning_label = rlabel_ptr;
        widget->append(std::move(expander));
    }

    if (role == "user") {
        widget->set_halign(Gtk::Align::END);
        widget->set_hexpand(false);
        widget->set_margin_start(50);
        widget->set_margin_end(12);

        auto card = Gtk::Box::create(Gtk::Orientation::VERTICAL, 0);
        card->add_css_class("card");

        auto label = Gtk::Label::create(content.c_str());
        label->set_wrap(true);
        label->set_xalign(0.0f);
        label->set_max_width_chars(40);
        label->set_use_markup(true);
        label->set_margin_start(10);
        label->set_margin_end(10);
        label->set_margin_top(6);
        label->set_margin_bottom(6);

        Gtk::Label *label_ptr = label;
        widget->m_label = label_ptr;
        card->append(std::move(label));
        widget->append(std::move(card));
    } else {
        widget->set_halign(Gtk::Align::FILL);
        widget->set_hexpand(true);
        widget->set_margin_start(12);
        widget->set_margin_end(12);

        auto label = Gtk::Label::create(content.c_str());
        label->set_wrap(true);
        label->set_xalign(0.0f);
        label->set_max_width_chars(80);
        label->set_use_markup(true);

        Gtk::Label *label_ptr = label;
        widget->m_label = label_ptr;
        widget->append(std::move(label));
    }

    return widget;
}

void MessageWidget::appendChunk(std::string_view chunk)
{
    if (!m_label) return;
    auto current = std::string(m_label->get_text());
    current.append(chunk);
    m_label->set_label(current.c_str());
}

void MessageWidget::appendReasoningChunk(std::string_view chunk)
{
    if (!m_reasoning_expander) {
        auto expander = Gtk::Expander::create("Thinking...");
        expander->set_expanded(false);

        auto rlabel = Gtk::Label::create("");
        rlabel->set_wrap(true);
        rlabel->set_xalign(0.0f);
        rlabel->set_max_width_chars(80);
        rlabel->add_css_class("dim-label");
        rlabel->set_use_markup(true);

        Gtk::Label *rlabel_ptr = rlabel;
        expander->set_child(std::move(rlabel).release_floating_ptr());
        m_reasoning_expander = expander;
        m_reasoning_label = rlabel_ptr;
        prepend(std::move(expander));
    }

    auto current = std::string(m_reasoning_label->get_text());
    current.append(chunk);
    m_reasoning_label->set_label(current.c_str());
}

} // namespace rook::gui
