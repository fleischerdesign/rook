#include <glib/gi18n.h>
#include "llm_settings_page.hpp"
#include "provider_dialog.hpp"
#include <peel/Adw/Adw.h>
#include <gtk/gtk.h>

using namespace peel;

namespace rook::gui {

std::unique_ptr<LlmSettingsPage> LlmSettingsPage::create(rook::ports::LlmPort &llm,
                                                           ChangeFn on_changed)
{
    auto page = std::unique_ptr<LlmSettingsPage>(new LlmSettingsPage());
    page->m_llm = &llm;
    page->m_on_changed = std::move(on_changed);
    return page;
}

void LlmSettingsPage::populate(peel::Adw::PreferencesGroup &group)
{
    auto heading = Gtk::Label::create(_("LLM Providers"));
    heading->set_xalign(0.0f);
    heading->add_css_class("title-2");
    group.add(std::move(heading).release_floating_ptr());

    auto desc = Gtk::Label::create(
        _("Configure language model backends. Rook supports Ollama (local), OpenAI, DeepSeek, and Anthropic."));
    desc->set_xalign(0.0f);
    desc->set_wrap(true);
    desc->add_css_class("dim-label");
    desc->set_margin_bottom(12);
    group.add(std::move(desc).release_floating_ptr());

    auto list = Gtk::ListBox::create();
    list->set_hexpand(true);
    list->set_vexpand(true);
    list->add_css_class("boxed-list");
    m_list = list;
    group.add(std::move(list).release_floating_ptr());

    auto add_btn = Gtk::Button::create_with_label(_("Add Provider"));
    add_btn->add_css_class("suggested-action");
    add_btn->set_halign(Gtk::Align::START);
    add_btn->set_margin_top(12);
    auto *self = this;
    add_btn->connect_clicked([self](Gtk::Button *) { self->onAddClicked(); });
    group.add(std::move(add_btn).release_floating_ptr());

    refreshList();
}

void LlmSettingsPage::refreshList()
{
    while (auto *row = m_list->get_row_at_index(0)) {
        m_list->remove(row);
    }

    auto providers = m_llm->listProviders();
    for (const auto &prov : providers) {
        auto row = Adw::ActionRow::create();
        row->set_title(prov.display_name.c_str());

        auto subtitle = prov.type + " / " + prov.default_model;
        row->set_subtitle(subtitle.c_str());

        auto sw = Gtk::Switch::create();
        sw->set_active(prov.enabled);
        sw->set_valign(Gtk::Align::CENTER);
        std::string pid = prov.id;
        auto *self = this;
        sw->connect_state_set([self, pid](Gtk::Switch *, bool state) -> bool {
            self->onToggleEnabled(pid, state);
            return false;
        });
        row->add_suffix(std::move(sw).release_floating_ptr());

        auto edit_btn = Gtk::Button::create_with_label(_("Edit"));
        edit_btn->set_valign(Gtk::Align::CENTER);
        edit_btn->connect_clicked([self, pid](Gtk::Button *) { self->onEditClicked(pid); });
        row->add_suffix(std::move(edit_btn).release_floating_ptr());

        auto del_btn = Gtk::Button::create_with_label(_("Delete"));
        del_btn->add_css_class("destructive-action");
        del_btn->set_valign(Gtk::Align::CENTER);
        del_btn->connect_clicked([self, pid](Gtk::Button *) { self->onDeleteClicked(pid); });
        row->add_suffix(std::move(del_btn).release_floating_ptr());

        m_list->append(std::move(row).release_floating_ptr());
    }
}

void LlmSettingsPage::onAddClicked()
{
    auto dialog = ProviderDialog::create();
    auto *self = this;
    dialog->connect_done([self](ProviderDialog *dlg) {
        if (!dlg->wasAccepted()) return;
        auto cfg = dlg->getConfig();
        auto info = rook::ports::ProviderRegistry::instance().find(cfg.type);
        self->m_llm->addProvider(rook::ports::LlmProviderConfig{
            .id = "",
            .display_name = info ? info->display_name : cfg.type,
            .type = cfg.type,
            .base_url = info ? info->base_url : "",
            .api_key = cfg.api_key,
            .default_model = info ? info->default_model : "",
            .enabled = true,
        });
        self->refreshList();
        self->m_on_changed();
        dlg->close();
    });
    dialog->present();
}

void LlmSettingsPage::onEditClicked(const std::string &provider_id)
{
    auto providers = m_llm->listProviders();
    for (const auto &prov : providers) {
        if (prov.id == provider_id) {
            ProviderConfig cfg{prov.type, prov.api_key};

            auto dialog = ProviderDialog::create(cfg);
            auto *self = this;
            dialog->connect_done([self, pid = provider_id](ProviderDialog *dlg) {
                if (!dlg->wasAccepted()) return;
                auto new_cfg = dlg->getConfig();
                auto info = rook::ports::ProviderRegistry::instance().find(new_cfg.type);
                self->m_llm->updateProvider(rook::ports::LlmProviderConfig{
                    .id = pid,
                    .display_name = info ? info->display_name : new_cfg.type,
                    .type = new_cfg.type,
                    .base_url = info ? info->base_url : "",
                    .api_key = new_cfg.api_key,
                    .default_model = info ? info->default_model : "",
                    .enabled = true,
                });
                self->refreshList();
                self->m_on_changed();
                dlg->close();
            });
            dialog->present();
            return;
        }
    }
}

void LlmSettingsPage::onDeleteClicked(const std::string &provider_id)
{
    m_llm->removeProvider(provider_id);
    refreshList();
    m_on_changed();
}

void LlmSettingsPage::onToggleEnabled(const std::string &provider_id, bool enabled)
{
    auto providers = m_llm->listProviders();
    for (const auto &prov : providers) {
        if (prov.id == provider_id) {
            auto updated = prov;
            updated.enabled = enabled;
            m_llm->updateProvider(updated);
            m_on_changed();
            return;
        }
    }
}

} // namespace rook::gui
