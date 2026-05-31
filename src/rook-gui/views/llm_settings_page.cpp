#include "llm_settings_page.hpp"

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(LlmSettingsPage, "RookLlmSettingsPage", Gtk::Box)
inline void LlmSettingsPage::Class::init() {}
inline void LlmSettingsPage::init(Class*) {
    append(Gtk::Label::create("LLM Settings (TODO)"));
}
FloatPtr<LlmSettingsPage> LlmSettingsPage::create() { return Object::create<LlmSettingsPage>(); }

} // namespace rook::gui
