#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <peel/signal.h>
#include <string>
#include <vector>
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

struct ProviderConfig {
    std::string type;
    std::string api_key;
};

class ProviderDialog final : public peel::Gtk::Window
{
    PEEL_SIMPLE_CLASS(ProviderDialog, peel::Gtk::Window)

    peel::Gtk::DropDown *m_type = nullptr;
    peel::Gtk::Entry *m_api_key = nullptr;
    ProviderConfig m_config;
    bool m_accepted = false;
    std::vector<std::string> m_type_ids;
    static peel::Signal<ProviderDialog, void(void)> sig_done;

    inline void init(Class *);
    void onSave(peel::Gtk::Button *);
    void onCancel(peel::Gtk::Button *);

public:
    static peel::FloatPtr<ProviderDialog> create(const ProviderConfig &existing = {});

    ProviderConfig getConfig() const { return m_config; }
    bool wasAccepted() const { return m_accepted; }

    PEEL_SIGNAL_CONNECT_METHOD(done, sig_done)
};

} // namespace rook::gui
