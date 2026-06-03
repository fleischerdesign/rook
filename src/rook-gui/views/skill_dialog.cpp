#include "skill_dialog.hpp"
#include <gtk/gtk.h>

using namespace peel;

namespace rook::gui {

Signal<SkillDialog, void(void)> SkillDialog::sig_done;

PEEL_CLASS_IMPL(SkillDialog, "RookSkillDialog", Gtk::Window)

inline void SkillDialog::Class::init()
{
    sig_done = Signal<SkillDialog, void(void)>::create("done");
}

inline void SkillDialog::init(Class *)
{
    set_title("Create Skill");
    set_modal(true);
    set_default_size(440, 400);

    auto content = Gtk::Box::create(Gtk::Orientation::VERTICAL, 8);
    content->set_margin_start(16);
    content->set_margin_end(16);
    content->set_margin_top(16);
    content->set_margin_bottom(16);

    auto name_label = Gtk::Label::create("Name:");
    name_label->set_xalign(0.0f);
    content->append(std::move(name_label));

    auto name_entry = Gtk::Entry::create();
    name_entry->set_placeholder_text("e.g. typescript-style");
    m_name_entry = name_entry;
    content->append(std::move(name_entry));

    auto desc_label = Gtk::Label::create("Description:");
    desc_label->set_xalign(0.0f);
    content->append(std::move(desc_label));

    auto desc_entry = Gtk::Entry::create();
    desc_entry->set_placeholder_text("Brief description of what this skill does");
    m_desc_entry = desc_entry;
    content->append(std::move(desc_entry));

    auto prompt_label = Gtk::Label::create("Prompt:");
    prompt_label->set_xalign(0.0f);
    content->append(std::move(prompt_label));

    auto prompt_view = Gtk::TextView::create();
    prompt_view->set_wrap_mode(Gtk::WrapMode::WORD);
    prompt_view->set_vexpand(true);
    prompt_view->set_monospace(true);
    prompt_view->set_left_margin(8);
    prompt_view->set_top_margin(4);
    m_prompt_view = prompt_view;

    auto prompt_sw = Gtk::ScrolledWindow::create();
    prompt_sw->set_child(std::move(prompt_view));
    content->append(std::move(prompt_sw));

    auto always_row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 12);
    auto always_label = Gtk::Label::create("Always ON in new chats:");
    always_label->set_xalign(0.0f);
    always_label->set_hexpand(true);
    auto always_switch = Gtk::Switch::create();
    m_always_on = always_switch;
    always_row->append(std::move(always_label));
    always_row->append(std::move(always_switch));
    content->append(std::move(always_row));

    auto error_label = Gtk::Label::create("");
    error_label->set_xalign(0.0f);
    error_label->set_use_markup(true);
    m_error = error_label;
    content->append(std::move(error_label));

    auto button_bar = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 8);
    button_bar->set_halign(Gtk::Align::END);
    button_bar->set_margin_top(8);

    auto cancel = Gtk::Button::create_with_label("Cancel");
    cancel->connect_clicked([this](Gtk::Button *) { onCancel(); });
    button_bar->append(std::move(cancel));

    auto save = Gtk::Button::create_with_label("Save Skill");
    save->add_css_class("suggested-action");
    save->connect_clicked([this](Gtk::Button *) { onSave(); });
    button_bar->append(std::move(save));

    content->append(std::move(button_bar));

    auto sw = Gtk::ScrolledWindow::create();
    sw->set_child(std::move(content).release_floating_ptr());
    set_child(std::move(sw).release_floating_ptr());
}

FloatPtr<SkillDialog> SkillDialog::create(
    const rook::adapters::extension::CustomSkill *existing)
{
    auto dlg = Object::create<SkillDialog>();

    if (existing) {
        dlg->m_edit_mode = true;
        dlg->set_title("Edit Skill");

        dlg->m_name_entry->set_text(existing->name.c_str());
        dlg->m_name_entry->set_sensitive(false);
        dlg->m_desc_entry->set_text(existing->description.c_str());

        auto* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(dlg->m_prompt_view));
        gtk_text_buffer_set_text(buf, existing->prompt.c_str(),
                                 static_cast<int>(existing->prompt.size()));

        dlg->m_always_on->set_active(existing->enabled);
    }

    return dlg;
}

bool SkillDialog::validateInput()
{
    if (strlen(m_name_entry->get_text()) == 0) {
        m_error->set_markup(
            "<span foreground=\"#e01b24\">Name is required.</span>");
        return false;
    }

    auto* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m_prompt_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    auto* text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);

    if (!text || !*text) {
        m_error->set_markup(
            "<span foreground=\"#e01b24\">Prompt is required.</span>");
        g_free(text);
        return false;
    }
    g_free(text);
    m_error->set_text("");
    return true;
}

void SkillDialog::onSave()
{
    if (!validateInput()) return;

    m_accepted = true;
    sig_done.emit(this);
    close();
}

void SkillDialog::onCancel()
{
    m_accepted = false;
    close();
}

rook::adapters::extension::CustomSkill SkillDialog::getSkill() const
{
    rook::adapters::extension::CustomSkill s;
    s.name = std::string(m_name_entry->get_text());
    s.description = std::string(m_desc_entry->get_text());

    auto* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m_prompt_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    auto* text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    if (text) {
        s.prompt = std::string(text);
        g_free(text);
    }

    s.enabled = m_always_on->get_active();

    return s;
}

} // namespace rook::gui
