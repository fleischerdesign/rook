#include "appearance_page.hpp"
using namespace peel;
namespace rook::gui {
PEEL_CLASS_IMPL(AppearancePage, "RookAppearancePage", Gtk::Box)
inline void AppearancePage::Class::init() {}
inline void AppearancePage::init(Class*) {
    append(Gtk::Label::create("Appearance (TODO)"));
}
FloatPtr<AppearancePage> AppearancePage::create() { return Object::create<AppearancePage>(); }
}
