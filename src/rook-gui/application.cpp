#include "application.hpp"
#include "window.hpp"
#include "views/chat_view.hpp"

namespace rook::gui {

RookApplication::RookApplication()
    : Gtk::Application("io.github.fleischerdesign.Rook")
{}

Glib::RefPtr<RookApplication> RookApplication::create() {
    return Glib::make_refptr_for_instance<RookApplication>(new RookApplication());
}

void RookApplication::on_activate() {
    if (get_windows().empty()) {
        auto window = std::make_unique<RookWindow>();
        add_window(*window);
        window->present();
        window.release(); // ownership transferred to GTK
    }
}

} // namespace rook::gui
