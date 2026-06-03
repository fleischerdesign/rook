#include "skills_page.hpp"
#include "skill_dialog.hpp"
#include "rook/ports/extension_port.hpp"
#include "rook/adapters/extension/extension_manager.hpp"
#include <peel/Adw/Adw.h>
#include <gtk/gtk.h>

using namespace peel;

namespace rook::gui {

Signal<SkillsPage, void(void)> SkillsPage::sig_changed;

PEEL_CLASS_IMPL(SkillsPage, "RookSkillsPage", Gtk::Box)

inline void SkillsPage::Class::init()
{
    sig_changed = Signal<SkillsPage, void(void)>::create("changed");
}

inline void SkillsPage::init(Class *)
{
    gtk_orientable_set_orientation(
        GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)),
        GTK_ORIENTATION_VERTICAL);
    set_spacing(12);
    set_margin_start(12);
    set_margin_end(12);
    set_margin_top(12);
    set_margin_bottom(12);
}

FloatPtr<SkillsPage> SkillsPage::create(
    std::vector<rook::adapters::extension::CustomSkill> *custom_skills,
    rook::ports::ExtensionPort *extensions)
{
    auto page = Object::create<SkillsPage>();
    page->m_custom_skills = custom_skills;
    page->m_extensions = extensions;

    auto heading = Gtk::Label::create("Skills");
    heading->set_xalign(0.0f);
    heading->add_css_class("title-2");
    page->append(std::move(heading));

    auto desc = Gtk::Label::create(
        "Skills are system prompts that influence how the AI responds. "
        "They can be created manually or come from installed extensions.");
    desc->set_xalign(0.0f);
    desc->add_css_class("dim-label");
    desc->set_wrap(true);
    desc->set_margin_bottom(4);
    page->append(std::move(desc));

    auto custom_header = Gtk::Label::create("");
    custom_header->set_xalign(0.0f);
    custom_header->set_use_markup(true);
    custom_header->set_markup(
        "<span weight=\"bold\" size=\"small\">Custom Skills</span>");
    custom_header->set_margin_top(8);
    page->append(std::move(custom_header));

    auto custom_list = Gtk::ListBox::create();
    custom_list->add_css_class("boxed-list");
    custom_list->set_selection_mode(Gtk::SelectionMode::NONE);
    page->m_custom_list = custom_list;
    page->append(std::move(custom_list));

    SkillsPage *raw_page = page;

    auto create_btn = Gtk::Button::create_with_label("+ Create Skill");
    create_btn->connect_clicked([raw_page](Gtk::Button *) {
        raw_page->onCreateSkill();
    });
    create_btn->set_halign(Gtk::Align::START);
    page->append(std::move(create_btn));

    auto ext_header = Gtk::Label::create("");
    ext_header->set_xalign(0.0f);
    ext_header->set_use_markup(true);
    ext_header->set_markup(
        "<span weight=\"bold\" size=\"small\">Extension Skills</span>");
    ext_header->set_margin_top(16);
    page->append(std::move(ext_header));

    auto ext_list = Gtk::ListBox::create();
    ext_list->add_css_class("boxed-list");
    ext_list->set_selection_mode(Gtk::SelectionMode::NONE);
    page->m_ext_list = ext_list;
    page->append(std::move(ext_list));

    page->refreshList();
    return page;
}

void SkillsPage::refreshList()
{
    while (auto *row = m_custom_list->get_row_at_index(0))
        m_custom_list->remove(row);
    while (auto *row = m_ext_list->get_row_at_index(0))
        m_ext_list->remove(row);

    for (auto& skill : *m_custom_skills) {
        auto expander = Adw::ExpanderRow::create();

        auto title = skill.name;
        if (!skill.description.empty())
            title += " · " + skill.description;
        expander->set_title(title.c_str());

        expander->set_subtitle(("Prompt: " + skill.prompt).c_str());

        auto toggle = Gtk::Switch::create();
        toggle->set_active(skill.enabled);
        std::string sname = skill.name;
        SkillsPage *raw = this;
        toggle->connect_state_set([raw, sname](Gtk::Switch *, bool state) {
            raw->onToggleAlwaysOn(sname, state);
            return false;
        });
        expander->add_suffix(std::move(toggle).release_floating_ptr());

        auto al_label = Gtk::Label::create("Always ON");
        al_label->add_css_class("dim-label");
        al_label->set_margin_end(4);
        expander->add_suffix(std::move(al_label).release_floating_ptr());

        auto del_btn = Gtk::Button::create_from_icon_name(
            "edit-delete-symbolic");
        del_btn->connect_clicked([raw, sname](Gtk::Button *) {
            raw->onDeleteSkill(sname);
        });
        expander->add_suffix(std::move(del_btn).release_floating_ptr());

        auto edit_btn = Gtk::Button::create_from_icon_name(
            "document-edit-symbolic");
        edit_btn->connect_clicked([raw, sname](Gtk::Button *) {
            raw->onEditSkill(sname);
        });
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

            std::string subtitle = "via " + ext.display_name
                + " v" + ext.version;
            if (!skill.description.empty())
                subtitle += " · " + skill.description;
            expander->set_subtitle(subtitle.c_str());

            auto toggle = Gtk::Switch::create();
            toggle->set_active(skill.enabled);
            std::string ext_name = ext.name;
            std::string skill_name = skill.name;
            SkillsPage *raw = this;
            toggle->connect_state_set([raw, ext_name, skill_name](
                Gtk::Switch *, bool state) {
                auto* em = dynamic_cast<
                    rook::adapters::extension::ExtensionManager*>(
                    raw->m_extensions);
                if (em) em->setSkillEnabled(ext_name, skill_name, state);
                raw->sig_changed.emit(raw);
                return false;
            });
            expander->add_suffix(std::move(toggle).release_floating_ptr());

            auto al_label = Gtk::Label::create("Always ON");
            al_label->add_css_class("dim-label");
            al_label->set_margin_end(4);
            expander->add_suffix(std::move(al_label).release_floating_ptr());

            m_ext_list->append(
                std::move(expander).release_floating_ptr());
        }
    }
}

void SkillsPage::onCreateSkill()
{
    auto dlg = SkillDialog::create();
    SkillDialog *raw_dlg = dlg;
    SkillsPage *raw = this;

    raw_dlg->connect_done([raw_dlg, raw](SkillDialog *) {
        if (!raw_dlg->wasAccepted()) return;
        auto skill = raw_dlg->getSkill();
        raw->m_custom_skills->push_back(std::move(skill));
        raw->refreshList();
        raw->sig_changed.emit(raw);
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
    SkillsPage *raw = this;
    std::string sname {name};

    raw_dlg->connect_done([raw_dlg, raw, sname](SkillDialog *) {
        if (!raw_dlg->wasAccepted()) return;
        auto skill = raw_dlg->getSkill();
        auto it = std::find_if(raw->m_custom_skills->begin(),
            raw->m_custom_skills->end(),
            [&sname](auto& s) { return s.name == sname; });
        if (it != raw->m_custom_skills->end()) *it = std::move(skill);
        raw->refreshList();
        raw->sig_changed.emit(raw);
        raw_dlg->close();
    });
    raw_dlg->present();
}

void SkillsPage::onDeleteSkill(std::string_view name)
{
    auto msg = std::string("Delete '") + std::string(name) + "'?\nThis cannot be undone.";

    auto dlg = Adw::AlertDialog::create("Delete Skill", msg.c_str());
    dlg->add_response("cancel", "Cancel");
    dlg->add_response("delete", "Delete");
    dlg->set_response_appearance("delete", Adw::ResponseAppearance::DESTRUCTIVE);
    dlg->set_default_response("cancel");
    dlg->set_close_response("cancel");

    SkillsPage *raw = this;
    std::string sname {name};
    dlg->connect_response([raw, sname](Adw::AlertDialog *, const char *resp) {
        if (!g_strcmp0(resp, "delete")) {
            std::erase_if(*raw->m_custom_skills,
                [&sname](auto& s) { return s.name == sname; });
            raw->refreshList();
            raw->sig_changed.emit(raw);
        }
    });
    dlg->present(this);
}

void SkillsPage::onToggleAlwaysOn(std::string_view name, bool enabled)
{
    for (auto& s : *m_custom_skills) {
        if (s.name == name) { s.enabled = enabled; break; }
    }
    sig_changed.emit(this);
}

} // namespace rook::gui
