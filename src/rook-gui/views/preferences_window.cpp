#include "preferences_window.hpp"
#include "llm_settings_page.hpp"
#include "voice_settings_page.hpp"
#include "appearance_page.hpp"
#include "mcp_settings_page.hpp"
#include "skills_page.hpp"
#include "extension_settings_page.hpp"
#include <glib/gi18n.h>
#include <adwaita.h>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(PreferencesWindow, "RookPreferencesDialog", Adw::PreferencesDialog)

struct PreferencesWindow::Impl {
    std::unique_ptr<LlmSettingsPage> llm;
    std::unique_ptr<McpSettingsPage> mcp;
    std::unique_ptr<SkillsPage> skills;
    std::unique_ptr<ExtensionSettingsPage> extensions;
    std::unique_ptr<VoiceSettingsPage> voice;
    std::unique_ptr<AppearancePage> appearance;
};

inline void PreferencesWindow::Class::init() {}

inline void PreferencesWindow::init(Class *) {}

FloatPtr<PreferencesWindow> PreferencesWindow::create(rook::ports::LlmPort &llm,
                                                        rook::adapters::mcp::McpServerManager *mcp,
                                                        rook::adapters::security::SecurityManager *security,
                                                        rook::ports::ExtensionPort *extensions,
                                                        std::vector<rook::adapters::extension::CustomSkill> *custom_skills,
                                                        std::function<void()> on_changed,
                                                        std::function<void(std::string_view)> on_before_uninstall,
                                                        rook::ports::WakewordPort* wakeword,
                                                        rook::ports::SpeechToTextPort* stt,
                                                        rook::ports::TextToSpeechPort* tts,
                                                        rook::ports::AudioDevicePort* audio_device)
{
    auto dialog = Object::create<PreferencesWindow>();
    dialog->set_title(_("Preferences"));

    adw_dialog_set_content_width(
        ADW_DIALOG(reinterpret_cast<::AdwDialog*>(static_cast<peel::Adw::PreferencesDialog*>(dialog))), 720);

    dialog->m_impl = std::make_unique<Impl>();

    dialog->m_impl->llm = LlmSettingsPage::create(llm, [on_changed] { on_changed(); });

    if (mcp && security)
        dialog->m_impl->mcp = McpSettingsPage::create(mcp, security, [on_changed] { on_changed(); });

    if (custom_skills)
        dialog->m_impl->skills = SkillsPage::create(custom_skills, extensions, [on_changed] { on_changed(); });

    if (extensions)
        dialog->m_impl->extensions = ExtensionSettingsPage::create(
            extensions, mcp,
            [on_changed] { on_changed(); },
            on_before_uninstall);

    dialog->m_impl->voice = VoiceSettingsPage::create(wakeword, stt, tts, audio_device,
                                                       [on_changed] { on_changed(); });
    dialog->m_impl->appearance = AppearancePage::create();

    auto llm_page = Adw::PreferencesPage::create();
    llm_page->set_title(_("Providers"));
    llm_page->set_icon_name("applications-system-symbolic");
    auto llm_group = Adw::PreferencesGroup::create();
    dialog->m_impl->llm->populate(*llm_group);
    llm_page->add(std::move(llm_group).release_floating_ptr());
    dialog->add(std::move(llm_page).release_floating_ptr());

    if (dialog->m_impl->mcp) {
        auto mcp_page = Adw::PreferencesPage::create();
        mcp_page->set_title(_("Tools"));
        mcp_page->set_icon_name("applications-engineering-symbolic");
        auto mcp_group = Adw::PreferencesGroup::create();
        dialog->m_impl->mcp->populate(*mcp_group);
        mcp_page->add(std::move(mcp_group).release_floating_ptr());
        dialog->add(std::move(mcp_page).release_floating_ptr());
    }

    if (dialog->m_impl->skills) {
        auto skills_page = Adw::PreferencesPage::create();
        skills_page->set_title(_("Skills"));
        skills_page->set_icon_name("applications-graphics-symbolic");
        auto skills_group = Adw::PreferencesGroup::create();
        dialog->m_impl->skills->populate(*skills_group);
        skills_page->add(std::move(skills_group).release_floating_ptr());
        dialog->add(std::move(skills_page).release_floating_ptr());
    }

    if (dialog->m_impl->extensions) {
        auto ext_page = Adw::PreferencesPage::create();
        ext_page->set_title(_("Extensions"));
        ext_page->set_icon_name("system-software-install-symbolic");
        auto ext_group = Adw::PreferencesGroup::create();
        dialog->m_impl->extensions->populate(*ext_group);
        ext_page->add(std::move(ext_group).release_floating_ptr());
        dialog->add(std::move(ext_page).release_floating_ptr());
    }

    auto voice_page = Adw::PreferencesPage::create();
    voice_page->set_title(_("Voice"));
    voice_page->set_icon_name("audio-input-microphone-symbolic");
    auto voice_group = Adw::PreferencesGroup::create();
    dialog->m_impl->voice->populate(*voice_group);
    voice_page->add(std::move(voice_group).release_floating_ptr());
    dialog->add(std::move(voice_page).release_floating_ptr());

    auto appearance_page = Adw::PreferencesPage::create();
    appearance_page->set_title(_("Appearance"));
    auto appearance_group = Adw::PreferencesGroup::create();
    dialog->m_impl->appearance->populate(*appearance_group);
    appearance_page->add(std::move(appearance_group).release_floating_ptr());
    dialog->add(std::move(appearance_page).release_floating_ptr());

    return dialog;
}

} // namespace rook::gui
