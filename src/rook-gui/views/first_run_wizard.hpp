#pragma once

#include <gtkmm.h>
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class FirstRunWizard : public Gtk::Window {
public:
    FirstRunWizard();
    ~FirstRunWizard() override = default;

    rook::ports::LlmConfig getConfig() const;

    using SlotDone = sigc::slot<void()>;
    SlotDone signal_done();

private:
    void setupUi();
    void onProviderChanged();
    void onFinish();

    Gtk::Box m_stack;
    Gtk::Box m_page1;
    Gtk::Box m_page2;

    Gtk::ComboBoxText m_provider;
    Gtk::Entry m_model;
    Gtk::Entry m_api_key;

    SlotDone m_signal_done;
};

} // namespace rook::gui
