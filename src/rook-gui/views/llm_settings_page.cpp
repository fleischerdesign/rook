#include "llm_settings_page.hpp"
#include <gtk/gtk.h>
using namespace peel;
namespace rook::gui {
PEEL_CLASS_IMPL(LlmSettingsPage, "RookLlmSettingsPage", Gtk::Box)
inline void LlmSettingsPage::Class::init() {}
inline void LlmSettingsPage::init(Class*) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)), GTK_ORIENTATION_VERTICAL);
    append(Gtk::Label::create("LLM Settings (TODO)"));
}
FloatPtr<LlmSettingsPage> LlmSettingsPage::create() { return Object::create<LlmSettingsPage>(); }

} // namespace rook::gui
