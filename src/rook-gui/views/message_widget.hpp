#pragma once

#include <gtkmm.h>

namespace rook::gui {

class MessageWidget : public Gtk::Box {
public:
    MessageWidget(std::string_view role, std::string_view content);
    ~MessageWidget() override = default;

    void appendChunk(std::string_view chunk);
    void setContent(std::string_view content);

    const std::string& role() const { return m_role; }

private:
    std::string m_role;
    Gtk::Label m_label;

    void applyStyle();
};

} // namespace rook::gui
