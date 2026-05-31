#include "provider_dialog.hpp"

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(ProviderDialog, "RookProviderDialog", Gtk::Window)

inline void ProviderDialog::Class::init()
{
}

inline void ProviderDialog::init(Class *)
{
    set_title("Provider Settings");
    set_default_size(400, 300);
    set_modal(true);

    auto box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 8);
    box->set_margin_start(16);
    box->set_margin_end(16);
    box->set_margin_top(16);
    box->set_margin_bottom(16);

    auto name_label = Gtk::Label::create("Name:");
    box->append(std::move(name_label));
    {
        auto entry = Gtk::Entry::create();
        entry->set_placeholder_text("Display Name");
        m_display_name = entry;
        box->append(std::move(entry));
    }

    auto url_label = Gtk::Label::create("Base URL:");
    box->append(std::move(url_label));
    {
        auto entry = Gtk::Entry::create();
        entry->set_placeholder_text("https://api.example.com");
        m_base_url = entry;
        box->append(std::move(entry));
    }

    auto key_label = Gtk::Label::create("API Key:");
    box->append(std::move(key_label));
    {
        auto entry = Gtk::Entry::create();
        entry->set_placeholder_text("API Key");
        entry->set_visibility(false);
        m_api_key = entry;
        box->append(std::move(entry));
    }

    auto model_label = Gtk::Label::create("Default Model:");
    box->append(std::move(model_label));
    {
        auto entry = Gtk::Entry::create();
        entry->set_placeholder_text("model-name");
        m_model = entry;
        box->append(std::move(entry));
    }

    auto btn_box = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
    btn_box->set_halign(Gtk::Align::END);
    btn_box->set_margin_top(8);

    auto cancel = Gtk::Button::create_with_label("Cancel");
    cancel->connect_clicked([this](Gtk::Button *) { onCancel(nullptr); });
    btn_box->append(std::move(cancel));

    auto save = Gtk::Button::create_with_label("Save");
    save->add_css_class("suggested-action");
    save->connect_clicked([this](Gtk::Button *) { onSave(nullptr); });
    btn_box->append(std::move(save));

    box->append(std::move(btn_box));
    set_child(std::move(box));
}

void ProviderDialog::onSave(Gtk::Button *)
{
    m_config.display_name = m_display_name->get_text();
    m_config.base_url = m_base_url->get_text();
    m_config.api_key = m_api_key->get_text();
    m_config.default_model = m_model->get_text();
    m_accepted = true;
    close();
}

void ProviderDialog::onCancel(Gtk::Button *)
{
    m_accepted = false;
    close();
}

FloatPtr<ProviderDialog> ProviderDialog::create(const ProviderConfig &existing)
{
    auto dlg = Object::create<ProviderDialog>();
    if (!existing.display_name.empty()) {
        dlg->m_display_name->set_text(existing.display_name.c_str());
        dlg->m_base_url->set_text(existing.base_url.c_str());
        dlg->m_model->set_text(existing.default_model.c_str());
        dlg->m_config.id = existing.id;
        dlg->m_config.type = existing.type;
    }
    return dlg;
}

ProviderConfig ProviderDialog::getConfig() const { return m_config; }

} // namespace rook::gui
