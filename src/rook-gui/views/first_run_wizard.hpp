#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <peel/signal.h>
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class FirstRunWizard final : public peel::Gtk::Window
{
    PEEL_SIMPLE_CLASS(FirstRunWizard, peel::Gtk::Window)

    peel::Gtk::DropDown *m_provider = nullptr;
    peel::Gtk::Entry *m_api_key = nullptr;
    rook::ports::LlmConfig m_config;
    static peel::Signal<FirstRunWizard, void(void)> sig_done;
    std::vector<std::string> m_provider_ids;

    inline void init(Class *);
    void onFinish(peel::Gtk::Button *);

public:
    static peel::FloatPtr<FirstRunWizard> create();

    rook::ports::LlmConfig getConfig() const { return m_config; }

    PEEL_SIGNAL_CONNECT_METHOD(done, sig_done)
};

} // namespace rook::gui
