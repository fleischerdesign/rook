#include "application.hpp"

using namespace peel;

int main(int argc, char* argv[]) {
    auto app = rook::gui::RookApplication::create();
    return app->run(argc, argv);
}
