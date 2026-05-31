#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <peel/class.h>
#include <peel/signal.h>
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class LlmSettingsPage final : public peel::Gtk::Box {
    PEEL_SIMPLE_CLASS(LlmSettingsPage, peel::Gtk::Box)

    rook::ports::LlmPort *m_llm = nullptr;
    peel::Gtk::ListBox *m_list = nullptr;
    static peel::Signal<LlmSettingsPage, void(void)> sig_changed;

    inline void init(Class *);
    void refreshList();
    void onAddClicked(peel::Gtk::Button *);
    void onEditClicked(const std::string &provider_id);
    void onDeleteClicked(const std::string &provider_id);
    void onToggleEnabled(const std::string &provider_id, bool enabled);
    peel::Gtk::Window *getParentWindow();

public:
    static peel::FloatPtr<LlmSettingsPage> create(rook::ports::LlmPort &llm);
    PEEL_SIGNAL_CONNECT_METHOD(changed, sig_changed)
};

} // namespace rook::gui
