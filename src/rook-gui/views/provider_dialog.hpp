#pragma once

#include <gtkmm.h>
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class ProviderDialog : public Gtk::Dialog {
public:
    ProviderDialog(Gtk::Window& parent, std::string editing_id);
    ~ProviderDialog() override = default;

    rook::ports::LlmProviderConfig getProvider() const;
    void setProvider(const rook::ports::LlmProviderConfig& provider);

private:
    void setupUi();
    void onTypeChanged();
    void onTestConnection();

    std::string m_editing_id;
    Gtk::Entry m_display_name;
    Gtk::ComboBoxText m_type;
    Gtk::Entry m_base_url;
    Gtk::Entry m_api_key;
    Gtk::Entry m_model;
    Gtk::Button m_test_button;
    Gtk::Label m_test_result;
};

} // namespace rook::gui
