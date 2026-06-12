#include "appearance_page.hpp"
#include <glib/gi18n.h>
#include <peel/Gio/Gio.h>
#include <unistd.h>
using namespace peel;
namespace rook::gui {

struct AppearancePage::Impl {
    peel::RefPtr<peel::Gio::Settings> settings;
};

AppearancePage::~AppearancePage() = default;

std::unique_ptr<AppearancePage> AppearancePage::create()
{
    auto page = std::unique_ptr<AppearancePage>(new AppearancePage());
    page->m_impl = std::make_unique<Impl>();
    page->m_impl->settings = Gio::Settings::create("io.github.fleischerdesign.Rook");
    return page;
}

void AppearancePage::populate(peel::Adw::PreferencesGroup &group)
{
    auto heading = Gtk::Label::create(_("Appearance"));
    heading->add_css_class("title-1");
    heading->set_xalign(0.0f);
    group.add(std::move(heading).release_floating_ptr());

    auto lang_box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 4);
    auto lang_label = Gtk::Label::create(_("Language"));
    lang_label->set_xalign(0.0f);
    lang_label->add_css_class("heading");
    lang_box->append(std::move(lang_label));

    const char *languages[] = {"Deutsch", "English", nullptr};
    auto model = Gtk::StringList::create(languages);
    auto dd = Gtk::DropDown::create(model, RefPtr<Gtk::Expression>());

    auto current_lang = m_impl->settings->get_string("language");
    Gtk::DropDown* dd_ptr = dd;
    dd->set_selected(g_strcmp0(current_lang, "en") == 0 ? 1 : 0);

    auto settings = m_impl->settings;
    dd->connect_notify(Gtk::DropDown::prop_selected(),
        [settings, dd_ptr](GObject::Object *, GObject::ParamSpec *) {
            unsigned sel = dd_ptr->get_selected();
            settings->set_string("language", sel == 1 ? "en" : "de");
            settings->sync();

            auto dlg = Adw::AlertDialog::create(
                _("Restart Required"),
                _("Rook must restart to apply the new language. Restart now?"));
            dlg->add_response("later", _("Later"));
            dlg->add_response("restart", _("Restart Now"));
            dlg->set_response_appearance("restart", Adw::ResponseAppearance::SUGGESTED);
            dlg->set_default_response("restart");
            dlg->set_close_response("later");

            dlg->connect_response([](Adw::AlertDialog *, const char *resp) {
                if (g_strcmp0(resp, "restart") == 0) {
                    execl("/proc/self/exe", "/proc/self/exe", nullptr);
                    _exit(1);
                }
            });

            dlg->present(nullptr);
        });

    lang_box->append(std::move(dd));
    group.add(std::move(lang_box).release_floating_ptr());
}

} // namespace rook::gui
