#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/class.h>

namespace rook::gui {

class VoiceSettingsPage final : public peel::Gtk::Box {
    PEEL_SIMPLE_CLASS(VoiceSettingsPage, peel::Gtk::Box)
    inline void init(Class*);
public:
    static peel::FloatPtr<VoiceSettingsPage> create();
};

} // namespace rook::gui
