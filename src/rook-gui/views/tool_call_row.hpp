#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <string>
#include <string_view>
#include <map>

namespace rook::gui {

class ToolCallRow : public peel::Gtk::Box
{
    PEEL_SIMPLE_CLASS(ToolCallRow, peel::Gtk::Box)

    peel::Gtk::Label *m_status_label = nullptr;
    peel::Gtk::Spinner *m_spinner = nullptr;
    peel::Gtk::Expander *m_expander = nullptr;
    peel::Gtk::Box *m_details_box = nullptr;
    peel::Gtk::Label *m_args_label = nullptr;
    peel::Gtk::Label *m_result_label = nullptr;
    std::string m_call_id;
    std::string m_tool_name;

    inline void init(Class *);
    inline void vfunc_dispose ();

    void buildArgsBox(std::string_view arguments);

public:
    static peel::FloatPtr<ToolCallRow> createPending(
        std::string_view name,
        std::string_view call_id);

    static peel::FloatPtr<ToolCallRow> createCompleted(
        std::string_view name,
        std::string_view arguments,
        std::string_view result,
        bool is_error);

    void setResult(std::string_view result, bool is_error);
    std::string_view callId() const { return m_call_id; }
};

} // namespace rook::gui
