#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <string>
#include <string_view>
#include "rook/adapters/extension/extension_manifest.hpp"

namespace rook::gui {

class SkillDialog : public peel::Gtk::Window
{
    PEEL_SIMPLE_CLASS(SkillDialog, peel::Gtk::Window)

    static peel::Signal<SkillDialog, void(void)> sig_done;

    peel::Gtk::Entry *m_name_entry = nullptr;
    peel::Gtk::Entry *m_desc_entry = nullptr;
    peel::Gtk::TextView *m_prompt_view = nullptr;
    peel::Gtk::Switch *m_always_on = nullptr;
    peel::Gtk::Label *m_error = nullptr;

    bool m_accepted = false;
    bool m_edit_mode = false;

    inline void init(Class *);
    void onSave();
    void onCancel();
    bool validateInput();

public:
    PEEL_SIGNAL_CONNECT_METHOD(done, sig_done)

    static peel::FloatPtr<SkillDialog> create(
        const rook::adapters::extension::CustomSkill *existing = nullptr);

    bool wasAccepted() const { return m_accepted; }
    rook::adapters::extension::CustomSkill getSkill() const;
};

} // namespace rook::gui
