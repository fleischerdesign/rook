#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <memory>

namespace rook::gui {

class AppearancePage {
public:
    static std::unique_ptr<AppearancePage> create();
    ~AppearancePage();
    void populate(peel::Adw::PreferencesGroup &group);

private:
    AppearancePage() = default;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::gui
