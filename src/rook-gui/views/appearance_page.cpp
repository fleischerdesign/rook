#include "appearance_page.hpp"
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
using namespace peel;
namespace rook::gui {
PEEL_CLASS_IMPL(AppearancePage, "RookAppearancePage", Gtk::Box)

struct AppearancePage::Impl {
    GSettings *settings = nullptr;
};

inline void AppearancePage::Class::init() {}

static void on_language_changed(GtkDropDown *dd, GParamSpec *, gpointer user_data) {
    auto *settings = static_cast<GSettings *>(user_data);
    guint sel = gtk_drop_down_get_selected(dd);
    g_settings_set_string(settings, "language", sel == 1 ? "en" : "de");
    g_settings_sync();
}

inline void AppearancePage::init(Class*) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)), GTK_ORIENTATION_VERTICAL);
    set_vexpand(true);
    set_spacing(12);

    m_impl = std::make_unique<Impl>();
    m_impl->settings = g_settings_new("io.github.fleischerdesign.Rook");

    auto heading = Gtk::Label::create(_("Appearance"));
    heading->add_css_class("title-1");
    heading->set_xalign(0.0f);
    append(std::move(heading));

    auto lang_box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 4);
    auto lang_label = Gtk::Label::create(_("Language"));
    lang_label->set_xalign(0.0f);
    lang_label->add_css_class("heading");
    lang_box->append(std::move(lang_label));

    const char *languages[] = {"Deutsch", "English", nullptr};
    auto model = Gtk::StringList::create(languages);
    auto dd = Gtk::DropDown::create(model, RefPtr<Gtk::Expression>());

    auto *raw_dd = reinterpret_cast<::GtkDropDown*>(static_cast<Gtk::DropDown*>(dd));

    gchar *current_lang = g_settings_get_string(m_impl->settings, "language");
    gtk_drop_down_set_selected(raw_dd, g_strcmp0(current_lang, "en") == 0 ? 1 : 0);
    g_free(current_lang);

    g_signal_connect(raw_dd, "notify::selected",
                     G_CALLBACK(on_language_changed), m_impl->settings);

    lang_box->append(std::move(dd));

    auto hint = Gtk::Label::create(_("Requires restart to take effect."));
    hint->add_css_class("dim-label");
    hint->set_xalign(0.0f);
    hint->set_wrap(true);
    lang_box->append(std::move(hint));

    append(std::move(lang_box));
}

FloatPtr<AppearancePage> AppearancePage::create() { return Object::create<AppearancePage>(); }
}