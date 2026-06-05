#include <glib/gi18n.h>
#include "tool_call_row.hpp"
#include <gtk/gtk.h>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(ToolCallRow, "RookToolCallRow", Gtk::Box)

inline void ToolCallRow::Class::init()
{
    set_css_name("toolcallrow");
}

inline void ToolCallRow::init(Class *)
{
    new (&m_call_id) std::string();
    new (&m_tool_name) std::string();
    gtk_orientable_set_orientation(
        GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)),
        GTK_ORIENTATION_VERTICAL);
    set_spacing(2);
    set_margin_start(12);
    set_margin_end(12);
    set_margin_top(4);
    set_margin_bottom(4);
    add_css_class("tool-call-row");
}

inline void ToolCallRow::vfunc_dispose()
{
    parent_vfunc_dispose<ToolCallRow>();
}

FloatPtr<ToolCallRow> ToolCallRow::createPending(
    std::string_view name,
    std::string_view call_id)
{
    auto row = Object::create<ToolCallRow>();
    row->m_call_id = call_id;
    row->m_tool_name = name;

    auto header = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);

    auto spinner = Gtk::Spinner::create();
    spinner->set_visible(true);
    spinner->start();
    row->m_spinner = spinner;
    header->append(std::move(spinner));

    auto label = Gtk::Label::create("");
    label->set_xalign(0.0f);
    label->set_use_markup(true);
    label->set_markup(
        (std::string(_("<span weight=\"bold\">Tool:</span> ")) + row->m_tool_name).c_str());
    row->m_status_label = label;
    header->append(std::move(label));

    row->append(std::move(header));

    auto expander = Gtk::Expander::create(_("Show details"));
    expander->set_expanded(false);

    auto box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 4);
    box->set_margin_top(4);
    box->set_margin_start(28);
    row->m_details_box = box;

    expander->set_child(std::move(box).release_floating_ptr());
    row->m_expander = expander;
    row->append(std::move(expander));

    return row;
}

FloatPtr<ToolCallRow> ToolCallRow::createCompleted(
    std::string_view name,
    std::string_view arguments,
    std::string_view result,
    bool is_error)
{
    auto row = createPending(name, "");
    if (!arguments.empty()) {
        row->buildArgsBox(arguments);
    }
    row->setResult(result, is_error);
    return row;
}

void ToolCallRow::buildArgsBox(std::string_view arguments)
{
    if (!m_details_box) return;

    auto args_header = Gtk::Label::create("");
    args_header->set_xalign(0.0f);
    args_header->set_use_markup(true);
    args_header->set_markup(_("<span weight=\"bold\">Arguments:</span>"));

    auto args_label = Gtk::Label::create(std::string(arguments).c_str());
    args_label->set_wrap(true);
    args_label->set_xalign(0.0f);
    args_label->set_selectable(true);
    args_label->add_css_class("dim-label");
    args_label->set_margin_bottom(8);
    m_args_label = args_label;

    m_details_box->append(std::move(args_header));
    m_details_box->append(std::move(args_label));
}

void ToolCallRow::setResult(std::string_view result, bool is_error)
{
    if (m_spinner) {
        m_spinner->stop();
        m_spinner->set_visible(false);
    }

    if (m_status_label) {
        m_status_label->set_markup(
            (std::string(_("<span weight=\"bold\">Tool used:</span> ")) + m_tool_name).c_str());
    }

    if (!m_details_box) return;

    auto result_header = Gtk::Label::create("");
    result_header->set_xalign(0.0f);
    result_header->set_use_markup(true);
    if (is_error) {
        result_header->set_markup(_("<span weight=\"bold\" foreground=\"#e01b24\">Error:</span>"));
    } else {
        result_header->set_markup(_("<span weight=\"bold\">Result:</span>"));
    }

    auto result_label = Gtk::Label::create(std::string(result).c_str());
    result_label->set_wrap(true);
    result_label->set_xalign(0.0f);
    result_label->set_selectable(true);
    result_label->set_max_width_chars(100);
    m_result_label = result_label;

    m_details_box->append(std::move(result_header));
    m_details_box->append(std::move(result_label));

    if (is_error) {
        add_css_class("tool-call-error");
    }
}

} // namespace rook::gui
