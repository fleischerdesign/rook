#include <glib/gi18n.h>
#include "skills_page.hpp"
#include "skill_dialog.hpp"
#include "rook/ports/extension_port.hpp"
#include "rook/adapters/extension/extension_manager.hpp"
#include <peel/Adw/Adw.h>
#include <gtk/gtk.h>

using namespace peel;

namespace rook::gui {

std::unique_ptr<SkillsPage> SkillsPage::create(
    std::vector<rook::adapters::extension::CustomSkill> *custom_skills,
    rook::ports::ExtensionPort *extensions,
    ChangeFn on_changed)
{
    auto page = std::unique_ptr<SkillsPage>(new SkillsPage());
    page->m_custom_skills = custom_skills;
    page->m_extensions = extensions;
    page->m_on_changed = std::move(on_changed);
    return page;
}

void SkillsPage::populate(peel::Adw::PreferencesGroup &group)
{
    auto heading = Gtk::Label::create(_("Skills"));
    heading->set_xalign(0.0f);
    heading->add_css_class("title-2");
    group.add(std::move(heading).release_floating_ptr());

    auto desc = Gtk::Label::create(
        _("Skills are system prompts that influence how the AI responds. They can be created manually or come from installed extensions."));
    desc->set_xalign(0.0f);
    desc->add_css_class("dim-label");
    desc->set_wrap(true);
    desc->set_margin_bottom(4);
    group.add(std::move(desc).release_floating_ptr());

    auto custom_header = Gtk::Label::create("");
    custom_header->set_xalign(0.0f);
    custom_header->set_use_markup(true);
    custom_header->set_markup(
        ("<span weight=\"bold\" size=\"small\">" + std::string(_("Custom Skills")) + "</span>").c_str());
    custom_header->set_margin_top(8);
    group.add(std::move(custom_header).release_floating_ptr());

    auto custom_list = Gtk::ListBox::create();
    custom_list->add_css_class("boxed-list");
    custom_list->set_selection_mode(Gtk::SelectionMode::NONE);
    m_custom_list = custom_list;
    group.add(std::move(custom_list).release_floating_ptr());

    auto *self = this;

    auto create_btn = Gtk::Button::create_with_label(_("+ Create Skill"));
    create_btn->connect_clicked([self](Gtk::Button *) { self->onCreateSkill(); });
    create_btn->set_halign(Gtk::Align::START);
    group.add(std::move(create_btn).release_floating_ptr());

    auto ext_header = Gtk::Label::create("");
    ext_header->set_xalign(0.0f);
    ext_header->set_use_markup(true);
    ext_header->set_markup(
        ("<span weight=\"bold\" size=\"small\">" + std::string(_("Extension Skills")) + "</span>").c_str());
    ext_header->set_margin_top(16);
    group.add(std::move(ext_header).release_floating_ptr());

    auto ext_list = Gtk::ListBox::create();
    ext_list->add_css_class("boxed-list");
    ext_list->set_selection_mode(Gtk::SelectionMode::NONE);
    m_ext_list = ext_list;
    group.add(std::move(ext_list).release_floating_ptr());

    refreshList();
}

void SkillsPage::refreshList()
{
    while (auto *row = m_custom_list->get_row_at_index(0))
        m_custom_list->remove(row);
    while (auto *row = m_ext_list->get_row_at_index(0))
        m_ext_list->remove(row);

    auto *self = this;

    for (auto& skill : *m_custom_skills) {
        auto expander = Adw::ExpanderRow::create();

        auto title = skill.name;
        if (!skill.description.empty())
            title += " " "\302\267" " " + skill.description;
        expander->set_title(title.c_str());

        expander->set_subtitle((std::string(_("Prompt: ")) + skill.prompt).c_str());

        auto toggle = Gtk::Switch::create();
        toggle->set_active(skill.enabled);
        std::string sname = skill.name;
        toggle->connect_state_set([self, sname](Gtk::Switch *, bool state) {
            self->onToggleAlwaysOn(sname, state);
            return false;
        });
        expander->add_suffix(std::move(toggle).release_floating_ptr());

        auto al_label = Gtk::Label::create(_("Always ON"));
        al_label->add_css_class("dim-label");
        al_label->set_margin_end(4);
        expander->add_suffix(std::move(al_label).release_floating_ptr());

        auto del_btn = Gtk::Button::create_from_icon_name("edit-delete-symbolic");
        del_btn->connect_clicked([self, sname](Gtk::Button *) { self->onDeleteSkill(sname); });
        expander->add_suffix(std::move(del_btn).release_floating_ptr());

        auto edit_btn = Gtk::Button::create_from_icon_name("document-edit-symbolic");
        edit_btn->connect_clicked([self, sname](Gtk::Button *) { self->onEditSkill(sname); });
        expander->add_suffix(std::move(edit_btn).release_floating_ptr());

        m_custom_list->append(std::move(expander).release_floating_ptr());
    }

    if (!m_extensions) return;

    auto installed = m_extensions->listInstalled();
    for (auto& ext : installed) {
        if (ext.skills.empty()) continue;

        for (auto& skill : ext.skills) {
            auto expander = Adw::ExpanderRow::create();

            auto title = skill.name;
            expander->set_title(title.c_str());

            std::string subtitle = _("via ") + ext.display_name
                + " v" + ext.version;
            if (!skill.description.empty())
                subtitle += " " "\302\267" " " + skill.description;
            expander->set_subtitle(subtitle.c_str());

            auto toggle = Gtk::Switch::create();
            toggle->set_active(skill.enabled);
            std::string ext_name = ext.name;
            std::string skill_name = skill.name;
            toggle->connect_state_set([self, ext_name, skill_name](Gtk::Switch *, bool state) {
                auto* em = dynamic_cast<rook::adapters::extension::ExtensionManager*>(self->m_extensions);
                if (em) em->setSkillEnabled(ext_name, skill_name, state);
                self->m_on_changed();
                return false;
            });
            expander->add_suffix(std::move(toggle).release_floating_ptr());

            auto al_label = Gtk::Label::create(_("Always ON"));
            al_label->add_css_class("dim-label");
            al_label->set_margin_end(4);
            expander->add_suffix(std::move(al_label).release_floating_ptr());

            m_ext_list->append(std::move(expander).release_floating_ptr());
        }
    }
}

void SkillsPage::onCreateSkill()
{
    auto dlg = SkillDialog::create();
    SkillDialog *raw_dlg = dlg;
    auto *self = this;

    raw_dlg->connect_done([raw_dlg, self](SkillDialog *) {
        if (!raw_dlg->wasAccepted()) return;
        auto skill = raw_dlg->getSkill();
        self->m_custom_skills->push_back(std::move(skill));
        self->refreshList();
        self->m_on_changed();
        raw_dlg->close();
    });
    raw_dlg->present();
}

void SkillsPage::onEditSkill(std::string_view name)
{
    auto it = std::find_if(m_custom_skills->begin(), m_custom_skills->end(),
        [name](auto& s) { return s.name == name; });
    if (it == m_custom_skills->end()) return;

    auto existing = *it;
    auto dlg = SkillDialog::create(&existing);
    SkillDialog *raw_dlg = dlg;
    auto *self = this;
    std::string sname {name};

    raw_dlg->connect_done([raw_dlg, self, sname](SkillDialog *) {
        if (!raw_dlg->wasAccepted()) return;
        auto skill = raw_dlg->getSkill();
        auto it = std::find_if(self->m_custom_skills->begin(),
            self->m_custom_skills->end(),
            [&sname](auto& s) { return s.name == sname; });
        if (it != self->m_custom_skills->end()) *it = std::move(skill);
        self->refreshList();
        self->m_on_changed();
        raw_dlg->close();
    });
    raw_dlg->present();
}

void SkillsPage::onDeleteSkill(std::string_view name)
{
    auto msg = std::string(_("Delete '")) + std::string(name) + std::string(_("'?\nThis cannot be undone."));

    auto dlg = Adw::AlertDialog::create(_("Delete Skill"), msg.c_str());
    dlg->add_response("cancel", _("Cancel"));
    dlg->add_response("delete", _("Delete"));
    dlg->set_response_appearance("delete", Adw::ResponseAppearance::DESTRUCTIVE);
    dlg->set_default_response("cancel");
    dlg->set_close_response("cancel");

    auto *self = this;
    std::string sname {name};
    dlg->connect_response([self, sname](Adw::AlertDialog *, const char *resp) {
        if (!g_strcmp0(resp, "delete")) {
            std::erase_if(*self->m_custom_skills,
                [&sname](auto& s) { return s.name == sname; });
            self->refreshList();
            self->m_on_changed();
        }
    });
    dlg->present(nullptr);
}

void SkillsPage::onToggleAlwaysOn(std::string_view name, bool enabled)
{
    for (auto& s : *m_custom_skills) {
        if (s.name == name) { s.enabled = enabled; break; }
    }
    m_on_changed();
}

} // namespace rook::gui
