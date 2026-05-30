#pragma once

#include <gtkmm.h>
#include <adwaita.h>
#include <memory>

namespace rook::gui {

class RookWindow;

class RookApplication : public Gtk::Application {
public:
    static Glib::RefPtr<RookApplication> create();
    ~RookApplication() override = default;

protected:
    RookApplication();
    void on_activate() override;
};

} // namespace rook::gui
