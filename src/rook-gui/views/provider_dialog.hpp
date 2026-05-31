#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <string>

namespace rook::gui {

struct ProviderConfig {
    std::string id;
    std::string display_name;
    std::string type;
    std::string base_url;
    std::string api_key;
    std::string default_model;
};

class ProviderDialog final : public peel::Gtk::Window
{
    PEEL_SIMPLE_CLASS(ProviderDialog, peel::Gtk::Window)

    peel::Gtk::Entry *m_display_name = nullptr;
    peel::Gtk::Entry *m_base_url = nullptr;
    peel::Gtk::Entry *m_api_key = nullptr;
    peel::Gtk::Entry *m_model = nullptr;
    ProviderConfig m_config;

    inline void init(Class *);
    void onSave(peel::Gtk::Button *);
    void onCancel(peel::Gtk::Button *);

public:
    static peel::FloatPtr<ProviderDialog> create(const ProviderConfig &existing = {});

    ProviderConfig getConfig() const;
    bool wasAccepted() const { return m_accepted; }

private:
    bool m_accepted = false;
};

} // namespace rook::gui
