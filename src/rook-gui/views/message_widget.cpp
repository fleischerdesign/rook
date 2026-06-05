#include <glib/gi18n.h>
#include "message_widget.hpp"
#include "markdown_renderer.hpp"
#include <gtk/gtk.h>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(MessageWidget, "RookMessageWidget", Gtk::Box)

static std::string escapeText(std::string_view text)
{
    char* escaped = g_markup_escape_text(text.data(), text.size());
    std::string result(escaped);
    g_free(escaped);
    return result;
}

inline void MessageWidget::Class::init()
{
    set_css_name("messagewidget");
}

inline void MessageWidget::init(Class *)
{
    new (&m_role) std::string();
    new (&m_raw_content) std::string();
    gtk_orientable_set_orientation(
        GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)),
        GTK_ORIENTATION_VERTICAL);
    set_spacing(2);
    set_margin_top(6);
    set_margin_bottom(6);
}

void MessageWidget::rebuildContent()
{
    while (auto* child = gtk_widget_get_first_child(
               GTK_WIDGET(m_content_box))) {
        gtk_box_remove(m_content_box, child);
    }

    auto* rendered = MarkdownRenderer::render(m_raw_content);
    gtk_box_append(m_content_box, GTK_WIDGET(rendered));

    gtk_widget_queue_resize(GTK_WIDGET(this));
}

FloatPtr<MessageWidget> MessageWidget::create(const std::string &role,
                                                const std::string &content,
                                                const std::string &reasoning_content)
{
    auto widget = Object::create<MessageWidget>();
    widget->m_role = role;
    widget->m_raw_content = content;

    if (!reasoning_content.empty()) {
        auto expander = Gtk::Expander::create(_("Thinking..."));
        expander->set_expanded(false);

        auto rlabel = Gtk::Label::create(escapeText(reasoning_content).c_str());
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

    auto* rendered = MarkdownRenderer::render(content);

    if (role == "user") {
        widget->set_halign(Gtk::Align::END);
        widget->set_hexpand(false);
        widget->set_margin_start(50);
        widget->set_margin_end(12);

        auto* inner = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
        widget->m_content_box = inner;
        gtk_widget_set_margin_start(GTK_WIDGET(inner), 10);
        gtk_widget_set_margin_end(GTK_WIDGET(inner), 10);
        gtk_widget_set_margin_top(GTK_WIDGET(inner), 6);
        gtk_widget_set_margin_bottom(GTK_WIDGET(inner), 6);
        gtk_box_append(inner, GTK_WIDGET(rendered));

        auto card = Gtk::Box::create(Gtk::Orientation::VERTICAL, 0);
        card->add_css_class("card");
        card->append(reinterpret_cast<peel::Gtk::Widget*>(inner));
        widget->append(std::move(card));
    } else {
        widget->set_halign(Gtk::Align::FILL);
        widget->set_hexpand(true);
        widget->set_margin_start(12);
        widget->set_margin_end(12);

        auto* raw = widget.operator MessageWidget*();
        widget->m_content_box = reinterpret_cast<::GtkBox*>(raw);
        widget->append(reinterpret_cast<peel::Gtk::Widget*>(rendered));
    }

    return widget;
}

void MessageWidget::appendChunk(std::string_view chunk)
{
    m_raw_content.append(chunk);
    rebuildContent();
}

void MessageWidget::appendReasoningChunk(std::string_view chunk)
{
    if (!m_reasoning_expander) {
        auto expander = Gtk::Expander::create(_("Thinking..."));
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
    current += escapeText(chunk);
    m_reasoning_label->set_markup(current.c_str());
}

} // namespace rook::gui
