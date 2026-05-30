#pragma once

#include <gtkmm.h>
#include <string>

namespace rook::gui {

class AppearancePage : public Gtk::Box {
public:
    explicit AppearancePage(std::string config_path);
    ~AppearancePage() override = default;

private:
    void setupUi();

    std::string m_config_path;
    Gtk::ComboBoxText m_language;
};

} // namespace rook::gui
