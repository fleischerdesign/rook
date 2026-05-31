#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <peel/class.h>
#include <functional>
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class PreferencesWindow final : public peel::Adw::PreferencesDialog {
    PEEL_SIMPLE_CLASS(PreferencesWindow, peel::Adw::PreferencesDialog)
    inline void init(Class*);
public:
    static peel::FloatPtr<PreferencesWindow> create(rook::ports::LlmPort &llm,
                                                      std::function<void()> on_changed);
};

} // namespace rook::gui
