#include "appearance_page.hpp"

namespace rook::gui {

AppearancePage::AppearancePage(std::string config_path)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 6)
    , m_config_path(std::move(config_path))
{
    setupUi();
}

void AppearancePage::setupUi() {
    set_margin(24);

    auto* heading = Gtk::make_managed<Gtk::Label>("Appearance");
    heading->set_xalign(0.0f);
    heading->add_css_class("title-2");
    append(*heading);

    auto makeRow = [](auto& label, auto& widget) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        auto* lbl = Gtk::make_managed<Gtk::Label>(label);
        lbl->set_xalign(0.0f);
        lbl->set_width_chars(12);
        row->append(*lbl);
        widget.set_hexpand(true);
        row->append(widget);
        return row;
    };

    auto* section = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    section->set_margin_top(12);

    auto* section_label = Gtk::make_managed<Gtk::Label>("Language");
    section_label->set_xalign(0.0f);
    section_label->add_css_class("title-4");
    section->append(*section_label);

    m_language.append("de", "Deutsch");
    m_language.append("en", "English");
    m_language.set_active_id("de");
    section->append(*makeRow("Interface", m_language));

    append(*section);
}

} // namespace rook::gui
