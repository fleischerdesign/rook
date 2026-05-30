#include "voice_settings_page.hpp"

namespace rook::gui {

VoiceSettingsPage::VoiceSettingsPage()
    : Gtk::Box(Gtk::Orientation::VERTICAL, 6)
{
    setupUi();
}

void VoiceSettingsPage::setupUi() {
    set_margin(24);

    auto* heading = Gtk::make_managed<Gtk::Label>("Voice");
    heading->set_xalign(0.0f);
    heading->add_css_class("title-2");
    append(*heading);

    auto* desc = Gtk::make_managed<Gtk::Label>(
        "Voice settings will be available in Phase 4 (Voice Pipeline).");
    desc->set_xalign(0.0f);
    desc->set_wrap(true);
    desc->add_css_class("dim-label");
    append(*desc);
}

} // namespace rook::gui
