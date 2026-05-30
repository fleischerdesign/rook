#pragma once

#include <gtkmm.h>
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class SettingsDialog : public Gtk::Dialog {
public:
    SettingsDialog(Gtk::Window& parent);
    ~SettingsDialog() override = default;

    rook::ports::LlmConfig getConfig() const;
    void setConfig(const rook::ports::LlmConfig& config);

private:
    void setupUi();
    void onProviderChanged();

    Gtk::ComboBoxText m_provider;
    Gtk::Entry m_model;
    Gtk::Entry m_api_key;
    Gtk::Entry m_system_prompt;
    Gtk::Scale m_temperature;
};

} // namespace rook::gui
