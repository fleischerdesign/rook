#include "preferences_window.hpp"
#include "llm_settings_page.hpp"
#include "voice_settings_page.hpp"
#include "appearance_page.hpp"
#include "mcp_settings_page.hpp"
#include "skills_page.hpp"
#include "extension_settings_page.hpp"

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(PreferencesWindow, "RookPreferencesDialog", Adw::PreferencesDialog)

inline void PreferencesWindow::Class::init() {}

inline void PreferencesWindow::init(Class *) {}

FloatPtr<PreferencesWindow> PreferencesWindow::create(rook::ports::LlmPort &llm,
                                                        rook::adapters::mcp::McpServerManager *mcp,
                                                        rook::adapters::security::SecurityManager *security,
                                                        rook::ports::ExtensionPort *extensions,
                                                        std::vector<rook::adapters::extension::CustomSkill> *custom_skills,
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

    if (mcp && security) {
        auto mcp_page = Adw::PreferencesPage::create();
        mcp_page->set_title("Tools");
        mcp_page->set_icon_name("applications-engineering-symbolic");

        auto mcp_group = Adw::PreferencesGroup::create();
        auto mcp_settings = McpSettingsPage::create(mcp, security);
        mcp_settings->connect_changed(
            [on_changed](McpSettingsPage *) { on_changed(); });
        mcp_group->add(std::move(mcp_settings).release_floating_ptr());
        mcp_page->add(std::move(mcp_group).release_floating_ptr());
        dialog->add(std::move(mcp_page).release_floating_ptr());
    }

    if (custom_skills) {
        auto skills_page = Adw::PreferencesPage::create();
        skills_page->set_title("Skills");
        skills_page->set_icon_name("applications-graphics-symbolic");

        auto skills_group = Adw::PreferencesGroup::create();
        auto skills_settings = SkillsPage::create(custom_skills, extensions);
        skills_settings->connect_changed(
            [on_changed](SkillsPage *) { on_changed(); });
        skills_group->add(std::move(skills_settings).release_floating_ptr());
        skills_page->add(std::move(skills_group).release_floating_ptr());
        dialog->add(std::move(skills_page).release_floating_ptr());
    }

    if (extensions) {
        auto ext_page = Adw::PreferencesPage::create();
        ext_page->set_title("Extensions");
        ext_page->set_icon_name("system-software-install-symbolic");

        auto ext_group = Adw::PreferencesGroup::create();
        auto ext_settings = ExtensionSettingsPage::create(extensions, mcp);
        ext_settings->connect_changed(
            [on_changed](ExtensionSettingsPage *) { on_changed(); });
        ext_group->add(std::move(ext_settings).release_floating_ptr());
        ext_page->add(std::move(ext_group).release_floating_ptr());
        dialog->add(std::move(ext_page).release_floating_ptr());
    }

    auto voice_page = Adw::PreferencesPage::create();
    voice_page->set_title("Voice");
    voice_page->set_icon_name("audio-input-microphone-symbolic");
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
