#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <string_view>
#include <vector>
#include "rook/adapters/extension/extension_manifest.hpp"

namespace rook::ports {
class ExtensionPort;
}

namespace rook::gui {

class SkillsPage : public peel::Gtk::Box
{
    PEEL_SIMPLE_CLASS(SkillsPage, peel::Gtk::Box)

    static peel::Signal<SkillsPage, void(void)> sig_changed;

    std::vector<rook::adapters::extension::CustomSkill> *m_custom_skills = nullptr;
    rook::ports::ExtensionPort *m_extensions = nullptr;
    peel::Gtk::ListBox *m_custom_list = nullptr;
    peel::Gtk::ListBox *m_ext_list = nullptr;

    inline void init(Class *);

    void refreshList();
    void onCreateSkill();
    void onEditSkill(std::string_view name);
    void onDeleteSkill(std::string_view name);
    void onToggleAlwaysOn(std::string_view name, bool enabled);

public:
    PEEL_SIGNAL_CONNECT_METHOD(changed, sig_changed)

    static peel::FloatPtr<SkillsPage> create(
        std::vector<rook::adapters::extension::CustomSkill> *custom_skills,
        rook::ports::ExtensionPort *extensions);
};

} // namespace rook::gui
