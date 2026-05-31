#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/class.h>

namespace rook::gui {

class LlmSettingsPage final : public peel::Gtk::Box {
    PEEL_SIMPLE_CLASS(LlmSettingsPage, peel::Gtk::Box)
    inline void init(Class*);
public:
    static peel::FloatPtr<LlmSettingsPage> create();
};

} // namespace rook::gui
