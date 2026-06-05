#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <memory>

namespace rook::gui {

class VoiceSettingsPage {
public:
    static std::unique_ptr<VoiceSettingsPage> create();
    void populate(peel::Adw::PreferencesGroup &group);
};

} // namespace rook::gui
