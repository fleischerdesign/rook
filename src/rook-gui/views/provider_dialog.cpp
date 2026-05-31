#include "provider_dialog.hpp"
#include "rook/ports/llm_port.hpp"

using namespace peel;

namespace rook::gui {

Signal<ProviderDialog, void(void)> ProviderDialog::sig_done;

PEEL_CLASS_IMPL(ProviderDialog, "RookProviderDialog", Gtk::Window)

inline void ProviderDialog::Class::init()
{
    sig_done = Signal<ProviderDialog, void(void)>::create("done");
}

inline void ProviderDialog::init(Class *)
{
    set_title("Add Provider");
    set_default_size(380, 220);
    set_modal(true);

    auto box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 12);
    box->set_margin_start(16);
    box->set_margin_end(16);
    box->set_margin_top(16);
    box->set_margin_bottom(16);

    auto type_label = Gtk::Label::create("Provider:");
    type_label->set_xalign(0.0f);
    box->append(std::move(type_label));

    const char *empty[] = {nullptr};
    RefPtr<Gtk::StringList> type_model = Gtk::StringList::create(empty);
    auto types = rook::ports::ProviderRegistry::instance().all();
    for (const auto &t : types) {
        type_model->append(t.display_name.c_str());
        m_type_ids.push_back(t.id);
    }
    {
        auto dd = Gtk::DropDown::create(type_model, RefPtr<Gtk::Expression>());
        if (type_model->get_n_items() > 0) dd->set_selected(0);
        m_type = dd;
        box->append(std::move(dd));
    }

    auto key_label = Gtk::Label::create("API Key:");
    key_label->set_xalign(0.0f);
    box->append(std::move(key_label));
    {
        auto entry = Gtk::Entry::create();
        entry->set_placeholder_text("Enter your API key");
        entry->set_visibility(false);
        m_api_key = entry;
        box->append(std::move(entry));
    }

    auto btn_box = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
    btn_box->set_halign(Gtk::Align::END);
    btn_box->set_margin_top(8);
    btn_box->set_hexpand(true);

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
    auto sel = m_type->get_selected();
    if (sel < m_type_ids.size())
        m_config.type = m_type_ids[sel];
    m_config.api_key = m_api_key->get_text();
    m_accepted = true;
    sig_done.emit(this);
}

void ProviderDialog::onCancel(Gtk::Button *)
{
    m_accepted = false;
    sig_done.emit(this);
}

FloatPtr<ProviderDialog> ProviderDialog::create(const ProviderConfig &existing)
{
    auto dlg = Object::create<ProviderDialog>();
    if (!existing.type.empty()) {
        dlg->set_title("Edit Provider");
        dlg->m_config = existing;
        if (!existing.api_key.empty())
            dlg->m_api_key->set_text(existing.api_key.c_str());
        for (size_t i = 0; i < dlg->m_type_ids.size(); ++i) {
            if (dlg->m_type_ids[i] == existing.type) {
                dlg->m_type->set_selected(i);
                break;
            }
        }
    }
    return dlg;
}

} // namespace rook::gui
