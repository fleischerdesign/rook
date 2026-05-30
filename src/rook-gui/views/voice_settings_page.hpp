#pragma once

#include <gtkmm.h>

namespace rook::gui {

class VoiceSettingsPage : public Gtk::Box {
public:
    VoiceSettingsPage();
    ~VoiceSettingsPage() override = default;

private:
    void setupUi();
};

} // namespace rook::gui
