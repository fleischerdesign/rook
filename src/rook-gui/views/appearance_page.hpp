#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <memory>

namespace rook::gui {

class AppearancePage final : public peel::Gtk::Box {
    PEEL_SIMPLE_CLASS(AppearancePage, peel::Gtk::Box)
    inline void init(Class*);
public:
    static peel::FloatPtr<AppearancePage> create();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::gui
