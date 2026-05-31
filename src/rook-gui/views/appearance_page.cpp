#include "appearance_page.hpp"
#include <gtk/gtk.h>
using namespace peel;
namespace rook::gui {
PEEL_CLASS_IMPL(AppearancePage, "RookAppearancePage", Gtk::Box)
inline void AppearancePage::Class::init() {}
inline void AppearancePage::init(Class*) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)), GTK_ORIENTATION_VERTICAL);
    set_vexpand(true);
    append(Gtk::Label::create("Appearance (TODO)"));
}
FloatPtr<AppearancePage> AppearancePage::create() { return Object::create<AppearancePage>(); }
}
