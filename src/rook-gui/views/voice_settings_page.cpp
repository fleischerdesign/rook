#include "voice_settings_page.hpp"
#include <gtk/gtk.h>
using namespace peel;
namespace rook::gui {
PEEL_CLASS_IMPL(VoiceSettingsPage, "RookVoiceSettingsPage", Gtk::Box)
inline void VoiceSettingsPage::Class::init() {}
inline void VoiceSettingsPage::init(Class*) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)), GTK_ORIENTATION_VERTICAL);
    set_vexpand(true);
    append(Gtk::Label::create("Voice Settings (TODO)"));
}
FloatPtr<VoiceSettingsPage> VoiceSettingsPage::create() { return Object::create<VoiceSettingsPage>(); }
}
