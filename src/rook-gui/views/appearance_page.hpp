#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/class.h>

namespace rook::gui {

class AppearancePage final : public peel::Gtk::Box {
    PEEL_SIMPLE_CLASS(AppearancePage, peel::Gtk::Box)
    inline void init(Class*);
public:
    static peel::FloatPtr<AppearancePage> create();
};

} // namespace rook::gui
