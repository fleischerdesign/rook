#include "llm_settings_page.hpp"
#include "provider_dialog.hpp"

namespace rook::gui {

LlmSettingsPage::LlmSettingsPage(rook::ports::LlmPort& llm)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 6)
    , m_llm(llm)
{
    setupUi();
    refreshList();
}

Gtk::Window* LlmSettingsPage::getParentWindow() {
    auto* root = get_root();
    return dynamic_cast<Gtk::Window*>(root);
}

void LlmSettingsPage::setupUi() {
    set_margin(24);

    auto* heading = Gtk::make_managed<Gtk::Label>("LLM Providers");
    heading->set_xalign(0.0f);
    heading->add_css_class("title-2");
    append(*heading);

    auto* desc = Gtk::make_managed<Gtk::Label>(
        "Configure language model backends. Rook supports Ollama (local), "
        "OpenAI, DeepSeek, and Anthropic.");
    desc->set_xalign(0.0f);
    desc->set_wrap(true);
    desc->add_css_class("dim-label");
    desc->set_margin_bottom(12);
    append(*desc);

    m_list.set_hexpand(true);
    m_list.set_vexpand(true);

    auto* scrolled = Gtk::make_managed<Gtk::ScrolledWindow>();
    scrolled->set_child(m_list);
    scrolled->set_vexpand(true);
    append(*scrolled);

    m_add_button.set_label("Add Provider");
    m_add_button.add_css_class("suggested-action");
    m_add_button.set_halign(Gtk::Align::START);
    m_add_button.signal_clicked().connect(
        sigc::mem_fun(*this, &LlmSettingsPage::onAddClicked));
    append(m_add_button);
}

void LlmSettingsPage::refreshList() {
    while (auto* row = m_list.get_row_at_index(0)) {
        m_list.remove(*row);
    }

    auto providers = m_llm.listProviders();
    if (providers.empty()) return;

    for (const auto& prov : providers) {
        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();

        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        box->set_margin(8);

        auto* info = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        info->set_hexpand(true);

        std::string title = prov.display_name;
        if (prov.is_default) title += " (Default)";

        auto* name_label = Gtk::make_managed<Gtk::Label>(title);
        name_label->set_xalign(0.0f);
        name_label->add_css_class("heading");
        info->append(*name_label);

        auto model_info = prov.type + " / " + prov.default_model;
        auto* model_label = Gtk::make_managed<Gtk::Label>(model_info);
        model_label->set_xalign(0.0f);
        model_label->add_css_class("dim-label");
        model_label->add_css_class("caption");
        info->append(*model_label);

        box->append(*info);

        auto* toggle = Gtk::make_managed<Gtk::Switch>();
        toggle->set_active(prov.enabled);
        toggle->set_valign(Gtk::Align::CENTER);

        std::string prov_id = prov.id;
        toggle->signal_state_set().connect(
            [this, prov_id](bool state) {
                onToggleEnabled(prov_id, state);
                return false;
            },
            false);

        if (prov.is_default) {
            toggle->set_sensitive(false);
        }

        box->append(*toggle);

        auto* edit_btn = Gtk::make_managed<Gtk::Button>("Edit");
        edit_btn->set_valign(Gtk::Align::CENTER);
        edit_btn->signal_clicked().connect(
            [this, prov_id]() { onEditClicked(prov_id); });
        box->append(*edit_btn);

        if (!prov.is_default) {
            auto* del_btn = Gtk::make_managed<Gtk::Button>("Delete");
            del_btn->set_valign(Gtk::Align::CENTER);
            del_btn->add_css_class("destructive-action");
            del_btn->signal_clicked().connect(
                [this, prov_id]() { onDeleteClicked(prov_id); });
            box->append(*del_btn);
        }

        row->set_child(*box);
        m_list.append(*row);
    }
}

void LlmSettingsPage::onAddClicked() {
    auto* parent = getParentWindow();
    if (!parent) return;

    auto* dialog = new ProviderDialog(*parent, "");
    dialog->signal_response().connect(
        [this, dialog](int response) {
            if (response == Gtk::ResponseType::OK) {
                m_llm.addProvider(dialog->getProvider());
                refreshList();
                m_signal_changed.emit();
            }
            delete dialog;
        });
    dialog->present();
}

void LlmSettingsPage::onEditClicked(const std::string& provider_id) {
    auto* parent = getParentWindow();
    if (!parent) return;

    auto providers = m_llm.listProviders();
    for (const auto& p : providers) {
        if (p.id == provider_id) {
            auto* dialog = new ProviderDialog(*parent, provider_id);
            dialog->setProvider(p);
            dialog->signal_response().connect(
                [this, dialog](int response) {
                    if (response == Gtk::ResponseType::OK) {
                        m_llm.updateProvider(dialog->getProvider());
                        refreshList();
                        m_signal_changed.emit();
                    }
                    delete dialog;
                });
            dialog->present();
            return;
        }
    }
}

void LlmSettingsPage::onDeleteClicked(const std::string& provider_id) {
    m_llm.removeProvider(provider_id);
    refreshList();
    m_signal_changed.emit();
}

void LlmSettingsPage::onSetDefault(const std::string& provider_id) {
    m_llm.setDefaultProvider(provider_id);
    refreshList();
}

void LlmSettingsPage::onToggleEnabled(const std::string& provider_id, bool enabled) {
    auto providers = m_llm.listProviders();
    for (auto& p : providers) {
        if (p.id == provider_id) {
            p.enabled = enabled;
            m_llm.updateProvider(p);
            break;
        }
    }
    refreshList();
}

} // namespace rook::gui
