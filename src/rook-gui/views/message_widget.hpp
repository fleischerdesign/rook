#pragma once

#include <gtkmm.h>

namespace rook::gui {

class MessageWidget : public Gtk::Box {
public:
    MessageWidget(std::string_view role, std::string_view content,
                  std::string_view reasoning = {});
    ~MessageWidget() override = default;

    void appendChunk(std::string_view chunk);
    void setContent(std::string_view content);

    const std::string& role() const { return m_role; }

private:
    std::string m_role;
    Gtk::Expander m_reasoning_expander;
    Gtk::Label m_reasoning_label;
    Gtk::Label m_label;

    void applyStyle();
};

} // namespace rook::gui
