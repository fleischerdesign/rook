#include "voice_settings_page.hpp"
using namespace peel;
namespace rook::gui {
PEEL_CLASS_IMPL(VoiceSettingsPage, "RookVoiceSettingsPage", Gtk::Box)
inline void VoiceSettingsPage::Class::init() {}
inline void VoiceSettingsPage::init(Class*) {
    append(Gtk::Label::create("Voice Settings (TODO)"));
}
FloatPtr<VoiceSettingsPage> VoiceSettingsPage::create() { return Object::create<VoiceSettingsPage>(); }
}
