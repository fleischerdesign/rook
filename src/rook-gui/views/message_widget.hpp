#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <string>

namespace rook::gui {

class MessageWidget final : public peel::Gtk::Box
{
    PEEL_SIMPLE_CLASS(MessageWidget, peel::Gtk::Box)

    std::string m_role;
    std::string m_raw_content;
    peel::Gtk::Box *m_content_box = nullptr;
    peel::Gtk::Expander *m_reasoning_expander = nullptr;
    peel::Gtk::Label *m_reasoning_label = nullptr;

    inline void init(Class *);

    void rebuildContent();

public:
    static peel::FloatPtr<MessageWidget> create(const std::string &role,
                                                  const std::string &content,
                                                  const std::string &reasoning_content = "");

    void appendChunk(std::string_view chunk);
    void appendReasoningChunk(std::string_view chunk);
    std::string role() const { return m_role; }
};

} // namespace rook::gui
