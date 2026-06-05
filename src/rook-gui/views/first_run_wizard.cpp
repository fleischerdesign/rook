#include <glib/gi18n.h>
#include "first_run_wizard.hpp"
#include "rook/ports/llm_port.hpp"

using namespace peel;

namespace rook::gui {

Signal<FirstRunWizard, void(void)> FirstRunWizard::sig_done;

PEEL_CLASS_IMPL(FirstRunWizard, "RookFirstRunWizard", Gtk::Window)

inline void FirstRunWizard::Class::init()
{
    sig_done = Signal<FirstRunWizard, void(void)>::create("done");
}

inline void FirstRunWizard::init(Class *)
{
    set_title(_("Welcome to Rook"));
    set_default_size(450, 350);
    set_modal(true);

    auto box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 12);
    box->set_margin_start(24);
    box->set_margin_end(24);
    box->set_margin_top(24);
    box->set_margin_bottom(24);

    auto title = Gtk::Label::create(_("Set Up Your AI Assistant"));
    title->add_css_class("title-1");
    box->append(std::move(title));

    auto subtitle = Gtk::Label::create(
        "Rook needs an LLM backend to function. "
        "You can change this later in Settings.");
    subtitle->add_css_class("dim-label");
    subtitle->set_wrap(true);
    box->append(std::move(subtitle));

    const char *empty[] = {nullptr};
    RefPtr<Gtk::StringList> model = Gtk::StringList::create(empty);
    auto types = rook::ports::ProviderRegistry::instance().all();
    for (const auto &t : types) {
        auto label = t.display_name;
        if (t.id != "ollama") label += " (API key required)";
        else label += " (local, free)";
        model->append(label.c_str());
        m_provider_ids.push_back(t.id);
    }

    auto provider_label = Gtk::Label::create(_("Provider:"));
    provider_label->set_xalign(0.0f);
    box->append(std::move(provider_label));

    {
        auto dd = Gtk::DropDown::create(model, RefPtr<Gtk::Expression>());
        if (model->get_n_items() > 0) {
            for (size_t i = 0; i < m_provider_ids.size(); ++i) {
                if (m_provider_ids[i] == "ollama") {
                    dd->set_selected(i);
                    break;
                }
            }
        }
        m_provider = dd;
        box->append(std::move(dd));
    }

    auto key_label = Gtk::Label::create(_("API Key:"));
    key_label->set_xalign(0.0f);
    box->append(std::move(key_label));

    {
        auto entry = Gtk::Entry::create();
        entry->set_placeholder_text(_("Enter your API key"));
        entry->set_visibility(false);
        m_api_key = entry;
        box->append(std::move(entry));
    }

    auto finish = Gtk::Button::create_with_label(_("Get Started"));
    finish->add_css_class("suggested-action");
    finish->add_css_class("pill");
    finish->set_halign(Gtk::Align::CENTER);
    finish->connect_clicked([this](Gtk::Button *) { onFinish(nullptr); });
    box->append(std::move(finish));

    set_child(std::move(box));
}

void FirstRunWizard::onFinish(Gtk::Button *)
{
    auto sel = m_provider->get_selected();
    if (sel < m_provider_ids.size()) {
        m_config.provider = m_provider_ids[sel];
    }
    m_config.api_key = m_api_key->get_text();
    sig_done.emit(this);
}

FloatPtr<FirstRunWizard> FirstRunWizard::create()
{
    return Object::create<FirstRunWizard>();
}

} // namespace rook::gui
