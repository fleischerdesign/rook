#include <glib/gi18n.h>
#include "voice_settings_page.hpp"
#include <peel/Adw/Adw.h>
#include <gtk/gtk.h>
using namespace peel;

namespace rook::gui {

std::unique_ptr<VoiceSettingsPage> VoiceSettingsPage::create()
{
    return std::unique_ptr<VoiceSettingsPage>(new VoiceSettingsPage());
}

void VoiceSettingsPage::populate(peel::Adw::PreferencesGroup &group)
{
    group.add(std::move(Gtk::Label::create(_("Voice Settings"))).release_floating_ptr());
}

} // namespace rook::gui
