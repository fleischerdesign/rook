#include "preferences_window.hpp"
#include "llm_settings_page.hpp"
#include "voice_settings_page.hpp"
#include "appearance_page.hpp"

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(PreferencesWindow, "RookPreferencesDialog", Adw::PreferencesDialog)

inline void PreferencesWindow::Class::init() {}

inline void PreferencesWindow::init(Class *) {}

FloatPtr<PreferencesWindow> PreferencesWindow::create(rook::ports::LlmPort &llm,
                                                        std::function<void()> on_changed)
{
    auto dialog = Object::create<PreferencesWindow>();
    dialog->set_title("Preferences");

    auto llm_page = Adw::PreferencesPage::create();
    llm_page->set_title("Providers");
    llm_page->set_icon_name("applications-system-symbolic");

    auto llm_group = Adw::PreferencesGroup::create();
    auto llm_settings = LlmSettingsPage::create(llm);
    llm_settings->connect_changed([on_changed](LlmSettingsPage *) { on_changed(); });
    llm_group->add(std::move(llm_settings).release_floating_ptr());
    llm_page->add(std::move(llm_group).release_floating_ptr());
    dialog->add(std::move(llm_page).release_floating_ptr());

    auto voice_page = Adw::PreferencesPage::create();
    voice_page->set_title("Voice");
    auto voice_group = Adw::PreferencesGroup::create();
    voice_group->add(VoiceSettingsPage::create().release_floating_ptr());
    voice_page->add(std::move(voice_group).release_floating_ptr());
    dialog->add(std::move(voice_page).release_floating_ptr());

    auto appearance_page = Adw::PreferencesPage::create();
    appearance_page->set_title("Appearance");
    auto appearance_group = Adw::PreferencesGroup::create();
    appearance_group->add(AppearancePage::create().release_floating_ptr());
    appearance_page->add(std::move(appearance_group).release_floating_ptr());
    dialog->add(std::move(appearance_page).release_floating_ptr());

    return dialog;
}

} // namespace rook::gui
