#include "llm_settings_page.hpp"
#include "provider_dialog.hpp"
#include <gtk/gtk.h>

using namespace peel;

namespace rook::gui {

Signal<LlmSettingsPage, void(void)> LlmSettingsPage::sig_changed;

PEEL_CLASS_IMPL(LlmSettingsPage, "RookLlmSettingsPage", Gtk::Box)

inline void LlmSettingsPage::Class::init()
{
    sig_changed = Signal<LlmSettingsPage, void(void)>::create("changed");
}

inline void LlmSettingsPage::init(Class *)
{
    gtk_orientable_set_orientation(
        GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)),
        GTK_ORIENTATION_VERTICAL);
    set_margin_start(24);
    set_margin_end(24);
    set_margin_top(24);
}

FloatPtr<LlmSettingsPage> LlmSettingsPage::create(rook::ports::LlmPort &llm)
{
    auto page = Object::create<LlmSettingsPage>();
    auto *raw = static_cast<LlmSettingsPage*>(page);
    raw->m_llm = &llm;

    auto heading = Gtk::Label::create("LLM Providers");
    heading->set_xalign(0.0f);
    heading->add_css_class("title-2");
    raw->append(std::move(heading));

    auto desc = Gtk::Label::create(
        "Configure language model backends. Rook supports Ollama (local), "
        "OpenAI, DeepSeek, and Anthropic.");
    desc->set_xalign(0.0f);
    desc->set_wrap(true);
    desc->add_css_class("dim-label");
    desc->set_margin_bottom(12);
    raw->append(std::move(desc));

    auto list = Gtk::ListBox::create();
    list->set_hexpand(true);
    list->set_vexpand(true);
    list->add_css_class("boxed-list");

    auto scrolled = Gtk::ScrolledWindow::create();
    scrolled->set_child(std::move(list));
    scrolled->set_vexpand(true);
    raw->m_list = static_cast<Gtk::ListBox*>(scrolled->get_child());
    raw->append(std::move(scrolled));

    auto add_btn = Gtk::Button::create_with_label("Add Provider");
    add_btn->add_css_class("suggested-action");
    add_btn->set_halign(Gtk::Align::START);
    add_btn->set_margin_top(12);
    add_btn->connect_clicked([raw](Gtk::Button *) { raw->onAddClicked(nullptr); });
    raw->append(std::move(add_btn));

    raw->refreshList();

    return page;
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
        sw->connect_state_set([this, pid](Gtk::Switch *, bool state) -> bool {
            onToggleEnabled(pid, state);
            return false;
        });
        row->add_suffix(std::move(sw).release_floating_ptr());

        auto edit_btn = Gtk::Button::create_with_label("Edit");
        edit_btn->set_valign(Gtk::Align::CENTER);
        edit_btn->connect_clicked([this, pid](Gtk::Button *) { onEditClicked(pid); });
        row->add_suffix(std::move(edit_btn).release_floating_ptr());

        auto del_btn = Gtk::Button::create_with_label("Delete");
        del_btn->add_css_class("destructive-action");
        del_btn->set_valign(Gtk::Align::CENTER);
        del_btn->connect_clicked([this, pid](Gtk::Button *) { onDeleteClicked(pid); });
        row->add_suffix(std::move(del_btn).release_floating_ptr());

        m_list->append(std::move(row).release_floating_ptr());
    }
}

Gtk::Window *LlmSettingsPage::getParentWindow()
{
    if (auto *root = get_root())
        return root->template cast<Gtk::Window>();
    return nullptr;
}

void LlmSettingsPage::onAddClicked(Gtk::Button *)
{
    auto dialog = ProviderDialog::create();
    dialog->connect_done([this](ProviderDialog *dlg) {
        if (!dlg->wasAccepted()) return;
        auto cfg = dlg->getConfig();
        auto info = rook::ports::ProviderRegistry::instance().find(cfg.type);
        m_llm->addProvider(rook::ports::LlmProviderConfig{
            .id = "",
            .display_name = info ? info->display_name : cfg.type,
            .type = cfg.type,
            .base_url = info ? info->base_url : "",
            .api_key = cfg.api_key,
            .default_model = info ? info->default_model : "",
            .enabled = true,
        });
        refreshList();
        sig_changed.emit(this);
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
            dialog->connect_done([this, pid = provider_id](ProviderDialog *dlg) {
                if (!dlg->wasAccepted()) return;
                auto new_cfg = dlg->getConfig();
                auto info = rook::ports::ProviderRegistry::instance().find(new_cfg.type);
                m_llm->updateProvider(rook::ports::LlmProviderConfig{
                    .id = pid,
                    .display_name = info ? info->display_name : new_cfg.type,
                    .type = new_cfg.type,
                    .base_url = info ? info->base_url : "",
                    .api_key = new_cfg.api_key,
                    .default_model = info ? info->default_model : "",
                    .enabled = true,
                });
                refreshList();
                sig_changed.emit(this);
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
    sig_changed.emit(this);
}

void LlmSettingsPage::onToggleEnabled(const std::string &provider_id, bool enabled)
{
    auto providers = m_llm->listProviders();
    for (const auto &prov : providers) {
        if (prov.id == provider_id) {
            auto updated = prov;
            updated.enabled = enabled;
            m_llm->updateProvider(updated);
            sig_changed.emit(this);
            return;
        }
    }
}

} // namespace rook::gui
