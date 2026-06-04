#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <functional>
#include <string>
#include <vector>
#include "rook/domain/events.hpp"

namespace rook::gui {

using PermissionDecisionCallback =
    std::function<void(std::string_view uuid,
                       std::vector<rook::domain::ToolCallPermissionDecision::Result>)>;

class PermissionBanner final : public peel::Gtk::Box
{
    PEEL_SIMPLE_CLASS(PermissionBanner, peel::Gtk::Box)
    friend class peel::Gio::Application;

    std::string m_request_uuid;
    std::vector<peel::Gtk::CheckButton*> m_checkboxes;
    std::vector<std::string> m_call_ids;

    inline void init(Class *);

public:
    PermissionDecisionCallback on_decision;

    std::string requestUuid() const { return m_request_uuid; }

    static peel::FloatPtr<PermissionBanner> create(
        const rook::domain::ToolCallPermissionRequest& req);
};

} // namespace rook::gui
