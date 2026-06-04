#include "permission_banner.hpp"
#include <sstream>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(PermissionBanner, "RookPermissionBanner", Gtk::Box)

inline void PermissionBanner::Class::init() {}

inline void PermissionBanner::init(Class *)
{
    gtk_orientable_set_orientation(
        GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)),
        GTK_ORIENTATION_VERTICAL);
    set_spacing(6);
    set_margin_start(12);
    set_margin_end(12);
    set_margin_top(8);
    set_margin_bottom(8);
    add_css_class("card");
}

FloatPtr<PermissionBanner> PermissionBanner::create(
    const rook::domain::ToolCallPermissionRequest& req)
{
    auto banner = Object::create<PermissionBanner>();
    auto *b = static_cast<PermissionBanner*>(banner);
    b->m_request_uuid = req.request_uuid;

    auto header = Gtk::Label::create(
        ("Rook requests " + std::to_string(req.calls.size())
         + " tool call" + (req.calls.size() > 1 ? "s" : ""))
        .c_str());
    header->set_xalign(0.0f);
    header->set_margin_bottom(4);
    b->append(std::move(header));

    for (auto& call : req.calls) {
        auto row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
        row->set_margin_start(6);

        auto check = Gtk::CheckButton::create();
        check->set_active(true);
        b->m_checkboxes.push_back(check);
        b->m_call_ids.push_back(call.call_id);
        row->append(std::move(check));

        auto info = Gtk::Box::create(Gtk::Orientation::VERTICAL, 2);
        auto name = Gtk::Label::create(
            ("<b>" + call.tool_name + "</b>").c_str());
        name->set_xalign(0.0f);
        name->set_use_markup(true);
        info->append(std::move(name));

        std::string args_preview = call.arguments;
        if (args_preview.size() > 120)
            args_preview = args_preview.substr(0, 120) + "...";
        std::string escaped;
        for (auto c : args_preview) {
            if (c == '&') escaped += "&amp;";
            else if (c == '<') escaped += "&lt;";
            else if (c == '>') escaped += "&gt;";
            else escaped += c;
        }
        auto args_label = Gtk::Label::create(escaped.c_str());
        args_label->set_xalign(0.0f);
        args_label->add_css_class("dim-label");
        args_label->set_ellipsize(
            static_cast<Pango::EllipsizeMode>(PANGO_ELLIPSIZE_END));
        args_label->set_max_width_chars(50);
        info->append(std::move(args_label));

        row->append(std::move(info));
        b->append(std::move(row));
    }

    auto btn_box = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
    btn_box->set_margin_top(4);
    btn_box->set_halign(Gtk::Align::END);

    auto deny_btn = Gtk::Button::create_with_label("Deny All");
    deny_btn->add_css_class("flat");
    deny_btn->connect_clicked([b](Gtk::Button *) {
        std::vector<rook::domain::ToolCallPermissionDecision::Result> results;
        for (size_t i = 0; i < b->m_checkboxes.size(); ++i) {
            (void)b->m_checkboxes[i];
            rook::domain::ToolCallPermissionDecision::Result r;
            r.call_id = b->m_call_ids[i];
            r.decision = 1; // Deny
            results.push_back(r);
        }
        b->on_decision(b->m_request_uuid, std::move(results));
    });
    btn_box->append(std::move(deny_btn));

    auto allow_btn = Gtk::Button::create_with_label("Allow Selected");
    allow_btn->add_css_class("suggested-action");
    allow_btn->connect_clicked([b](Gtk::Button *) {
        std::vector<rook::domain::ToolCallPermissionDecision::Result> results;
        for (size_t i = 0; i < b->m_checkboxes.size(); ++i) {
            rook::domain::ToolCallPermissionDecision::Result r;
            r.call_id = b->m_call_ids[i];
            r.decision = b->m_checkboxes[i]->get_active() ? 0 : 1;
            results.push_back(r);
        }
        b->on_decision(b->m_request_uuid, std::move(results));
    });
    btn_box->append(std::move(allow_btn));

    auto always_btn = Gtk::Button::create_with_label("Always Allow");
    always_btn->add_css_class("flat");
    always_btn->connect_clicked([b](Gtk::Button *) {
        std::vector<rook::domain::ToolCallPermissionDecision::Result> results;
        for (size_t i = 0; i < b->m_checkboxes.size(); ++i) {
            rook::domain::ToolCallPermissionDecision::Result r;
            r.call_id = b->m_call_ids[i];
            r.decision = b->m_checkboxes[i]->get_active() ? 2 : 1;
            results.push_back(r);
        }
        b->on_decision(b->m_request_uuid, std::move(results));
    });
    btn_box->append(std::move(always_btn));

    b->append(std::move(btn_box));
    return banner;
}

} // namespace rook::gui
