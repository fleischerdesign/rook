#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>
#include "rook/adapters/extension/extension_manifest.hpp"

namespace rook::ports {
class ExtensionPort;
}

namespace rook::gui {

class SkillsPage {
public:
    using ChangeFn = std::function<void()>;

    static std::unique_ptr<SkillsPage> create(
        std::vector<rook::adapters::extension::CustomSkill> *custom_skills,
        rook::ports::ExtensionPort *extensions,
        ChangeFn on_changed);

    void populate(peel::Adw::PreferencesGroup &group);

private:
    SkillsPage() = default;

    std::vector<rook::adapters::extension::CustomSkill> *m_custom_skills = nullptr;
    rook::ports::ExtensionPort *m_extensions = nullptr;
    peel::Gtk::ListBox *m_custom_list = nullptr;
    peel::Gtk::ListBox *m_ext_list = nullptr;
    ChangeFn m_on_changed;

    void refreshList();
    void onCreateSkill();
    void onEditSkill(std::string_view name);
    void onDeleteSkill(std::string_view name);
    void onToggleAlwaysOn(std::string_view name, bool enabled);
};

} // namespace rook::gui
