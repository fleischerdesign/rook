#include "application.hpp"
#include <adwaita.h>

int main(int argc, char* argv[]) {
    adw_init();

    auto app = rook::gui::RookApplication::create();
    return app->run(argc, argv);
}
