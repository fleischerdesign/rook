#pragma once

#include <peel/Gtk/Gtk.h>
#include <string>
#include <vector>

struct cmark_node;

namespace rook::gui {

struct PangoAttr {
    std::string open;
    std::string close;
};

class MarkdownRenderer {
public:
    static peel::FloatPtr<peel::Gtk::Widget> render(const std::string& markdown);

private:
    static peel::FloatPtr<peel::Gtk::Widget> renderBlock(cmark_node* node);
    static std::string renderInlinePango(cmark_node* node,
        const std::vector<PangoAttr>& attrs = {});

    static std::string escapeText(std::string_view text);
    static std::string headingSize(int level);

    static constexpr int kCodeBlockMargin = 6;
};

} // namespace rook::gui
