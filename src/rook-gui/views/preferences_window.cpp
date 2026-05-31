#include "preferences_window.hpp"
#include "llm_settings_page.hpp"
#include "voice_settings_page.hpp"
#include "appearance_page.hpp"

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(PreferencesWindow, "RookPreferencesDialog", Adw::PreferencesDialog)
inline void PreferencesWindow::Class::init() {}
inline void PreferencesWindow::init(Class*) {
    set_title("Preferences");

    auto llm_page = Adw::PreferencesPage::create();
    llm_page->set_title("LLM");
    llm_page->set_icon_name("applications-system-symbolic");
    auto llm_group = Adw::PreferencesGroup::create();
    llm_group->add(LlmSettingsPage::create().release_floating_ptr());
    llm_page->add(std::move(llm_group).release_floating_ptr());
    add(std::move(llm_page).release_floating_ptr());

    auto voice_page = Adw::PreferencesPage::create();
    voice_page->set_title("Voice");
    auto voice_group = Adw::PreferencesGroup::create();
    voice_group->add(VoiceSettingsPage::create().release_floating_ptr());
    voice_page->add(std::move(voice_group).release_floating_ptr());
    add(std::move(voice_page).release_floating_ptr());

    auto appearance_page = Adw::PreferencesPage::create();
    appearance_page->set_title("Appearance");
    auto appearance_group = Adw::PreferencesGroup::create();
    appearance_group->add(AppearancePage::create().release_floating_ptr());
    appearance_page->add(std::move(appearance_group).release_floating_ptr());
    add(std::move(appearance_page).release_floating_ptr());
}
FloatPtr<PreferencesWindow> PreferencesWindow::create() {
    return Object::create<PreferencesWindow>();
}

} // namespace rook::gui
