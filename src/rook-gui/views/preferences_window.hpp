#pragma once

#include <gtkmm.h>
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class LlmSettingsPage;
class VoiceSettingsPage;
class AppearancePage;

class PreferencesWindow : public Gtk::Window {
public:
    PreferencesWindow(Gtk::Window& parent, rook::ports::LlmPort& llm,
                      const std::string& config_path);
    ~PreferencesWindow() override = default;

    sigc::signal<void()>& signal_changed() { return m_signal_changed; }

private:
    void setupUi();
    void onSidebarRowActivated(Gtk::ListBoxRow* row);

    rook::ports::LlmPort& m_llm;
    std::string m_config_path;
    sigc::signal<void()> m_signal_changed;

    Gtk::ListBox m_sidebar;
    Gtk::Stack m_stack;

    LlmSettingsPage* m_llm_page = nullptr;
    VoiceSettingsPage* m_voice_page = nullptr;
    AppearancePage* m_appearance_page = nullptr;
};

} // namespace rook::gui
