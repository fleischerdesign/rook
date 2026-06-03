#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <string>

namespace rook::gui {

class ExtensionInstallDialog : public peel::Gtk::Window
{
    PEEL_SIMPLE_CLASS(ExtensionInstallDialog, peel::Gtk::Window)

    static peel::Signal<ExtensionInstallDialog, void(void)> sig_done;

    peel::Gtk::Entry *m_url_entry = nullptr;
    peel::Gtk::Label *m_status_label = nullptr;
    peel::Gtk::Button *m_install_button = nullptr;
    std::string m_url;
    bool m_accepted = false;

    inline void init(Class *);

    void onInstall();

public:
    PEEL_SIGNAL_CONNECT_METHOD(done, sig_done)

    static peel::FloatPtr<ExtensionInstallDialog> create();

    bool wasAccepted() const { return m_accepted; }
    std::string url() const { return m_url; }
};

} // namespace rook::gui
