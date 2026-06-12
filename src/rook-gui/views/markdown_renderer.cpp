#include "markdown_renderer.hpp"
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <spdlog/spdlog.h>
#include <peel/Gtk/Gtk.h>
#include <cstdlib>

using namespace peel;

namespace rook::gui {

#define CMARK_NODE_TABLE             (CMARK_NODE_TYPE_BLOCK | 0x000c)
#define CMARK_NODE_TABLE_ROW         (CMARK_NODE_TYPE_BLOCK | 0x000d)
#define CMARK_NODE_TABLE_CELL        (CMARK_NODE_TYPE_BLOCK | 0x000e)
#define CMARK_NODE_STRIKETHROUGH     (CMARK_NODE_TYPE_INLINE | 0x000c)
#define CMARK_NODE_TASK_LIST_ITEM    (CMARK_NODE_TYPE_BLOCK | 0x000f)

std::string MarkdownRenderer::escapeText(std::string_view text)
{
    char* escaped = g_markup_escape_text(text.data(), text.size());
    std::string result(escaped);
    g_free(escaped);
    return result;
}

std::string MarkdownRenderer::headingSize(int level)
{
    switch (level) {
    case 1: return "xx-large";
    case 2: return "x-large";
    case 3: return "large";
    case 4: return "medium";
    case 5: return "small";
    default: return "x-small";
    }
}

std::string MarkdownRenderer::renderInlinePango(cmark_node* node,
    const std::vector<PangoAttr>& attrs)
{
    std::string result;

    for (auto& a : attrs)
        result += a.open;

    for (cmark_node* child = cmark_node_first_child(node);
         child; child = cmark_node_next(child)) {

        auto type = static_cast<int>(cmark_node_get_type(child));

        switch (type) {
        case CMARK_NODE_TEXT: {
            result += escapeText(cmark_node_get_literal(child));
            break;
        }
        case CMARK_NODE_SOFTBREAK:
            result += '\n';
            break;
        case CMARK_NODE_LINEBREAK:
            result += '\n';
            break;
        case CMARK_NODE_CODE:
            result += "<tt>";
            result += escapeText(cmark_node_get_literal(child));
            result += "</tt>";
            break;
        case CMARK_NODE_STRONG:
            result += renderInlinePango(child,
                {PangoAttr{"<b>", "</b>"}});
            break;
        case CMARK_NODE_EMPH:
            result += renderInlinePango(child,
                {PangoAttr{"<i>", "</i>"}});
            break;
        case CMARK_NODE_STRIKETHROUGH:
            result += renderInlinePango(child,
                {PangoAttr{"<s>", "</s>"}});
            break;
        case CMARK_NODE_LINK: {
            auto* url = cmark_node_get_url(child);
            std::string href = url ? escapeText(url) : "";
            if (!href.empty()) {
                result += "<a href=\"";
                result += href;
                result += "\">";
            }
            result += renderInlinePango(child);
            if (!href.empty())
                result += "</a>";
            break;
        }
        case CMARK_NODE_IMAGE: {
            auto* url = cmark_node_get_url(child);
            auto* title = cmark_node_get_title(child);
            if (url) {
                result += "[image: ";
                result += escapeText(title ? title : url);
                result += "]";
            }
            break;
        }
        default:
            result += renderInlinePango(child);
            break;
        }
    }

    for (auto it = attrs.rbegin(); it != attrs.rend(); ++it)
        result += it->close;

    return result;
}

FloatPtr<Gtk::Widget> MarkdownRenderer::renderBlock(cmark_node* node)
{
    auto type = static_cast<int>(cmark_node_get_type(node));

    switch (type) {
    case CMARK_NODE_PARAGRAPH: {
        auto label = Gtk::Label::create(nullptr);
        label->set_wrap(true);
        label->set_xalign(0.0f);
        auto markup = renderInlinePango(node);
        label->set_markup(markup.c_str());
        label->set_margin_top(2);
        label->set_margin_bottom(2);
        return std::move(label).template cast<Gtk::Widget>();
    }

    case CMARK_NODE_HEADING: {
        auto label = Gtk::Label::create(nullptr);
        label->set_wrap(true);
        label->set_xalign(0.0f);
        int level = cmark_node_get_heading_level(node);
        std::string markup = "<span size=\"";
        markup += headingSize(level);
        markup += "\" weight=\"bold\">";
        markup += renderInlinePango(node);
        markup += "</span>";
        label->set_markup(markup.c_str());
        label->set_margin_top(6);
        label->set_margin_bottom(2);
        return std::move(label).template cast<Gtk::Widget>();
    }

    case CMARK_NODE_CODE_BLOCK: {
        const char* literal = cmark_node_get_literal(node);
        std::string code = literal ? std::string(literal) : "";

        auto buf = Gtk::TextBuffer::create(nullptr);
        buf->set_text(code.c_str(), static_cast<int>(code.size()));

        auto tv = Gtk::TextView::create_with_buffer(
            static_cast<peel::Gtk::TextBuffer*>(buf));
        tv->set_editable(false);
        tv->set_monospace(true);
        tv->set_wrap_mode(Gtk::WrapMode::WORD);
        tv->set_margin_start(kCodeBlockMargin);
        tv->set_margin_end(kCodeBlockMargin);
        tv->set_margin_top(kCodeBlockMargin);
        tv->set_margin_bottom(kCodeBlockMargin);

        auto frame = Gtk::Frame::create(nullptr);
        frame->set_child(std::move(tv));
        frame->add_css_class("code-block");
        frame->set_margin_top(4);
        frame->set_margin_bottom(4);
        frame->set_hexpand(true);

        return std::move(frame).template cast<Gtk::Widget>();
    }

    case CMARK_NODE_BLOCK_QUOTE: {
        auto box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 0);
        box->add_css_class("blockquote");
        box->set_margin_start(6);
        box->set_hexpand(true);

        for (cmark_node* child = cmark_node_first_child(node);
             child; child = cmark_node_next(child)) {
            auto widget = renderBlock(child);
            if (widget)
                box->append(std::move(widget));
        }
        return std::move(box).template cast<Gtk::Widget>();
    }

    case CMARK_NODE_LIST: {
        auto box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 0);
        box->set_margin_start(12);

        cmark_list_type list_type = cmark_node_get_list_type(node);
        int n = static_cast<int>(cmark_node_get_list_start(node));

        for (cmark_node* item = cmark_node_first_child(node);
             item; item = cmark_node_next(item)) {

            auto row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 4);
            row->set_hexpand(true);

            char prefix[16];
            if (list_type == CMARK_ORDERED_LIST)
                std::snprintf(prefix, sizeof(prefix), "%d.", n++);
            else
                std::snprintf(prefix, sizeof(prefix), "\u2022");

            auto bullet = Gtk::Label::create(prefix);
            bullet->set_valign(Gtk::Align::START);
            bullet->set_margin_top(1);
            row->append(std::move(bullet));

            for (cmark_node* item_child = cmark_node_first_child(item);
                 item_child; item_child = cmark_node_next(item_child)) {
                auto widget = renderBlock(item_child);
                if (widget) {
                    widget->set_hexpand(true);
                    row->append(std::move(widget));
                }
            }

            box->append(std::move(row));
        }
        return std::move(box).template cast<Gtk::Widget>();
    }

    case CMARK_NODE_TABLE: {
        uint16_t ncols = cmark_gfm_extensions_get_table_columns(node);
        if (ncols == 0) ncols = 1;
        auto* alignments = cmark_gfm_extensions_get_table_alignments(node);

        auto grid = Gtk::Grid::create();
        grid->set_column_spacing(12);
        grid->set_row_spacing(2);
        grid->set_margin_top(4);
        grid->set_margin_bottom(4);
        grid->set_hexpand(true);

        int row_idx = 0;
        for (cmark_node* trow = cmark_node_first_child(node);
             trow; trow = cmark_node_next(trow)) {

            bool is_header = cmark_gfm_extensions_get_table_row_is_header(trow);
            int col_idx = 0;
            for (cmark_node* cell = cmark_node_first_child(trow);
                 cell && col_idx < static_cast<int>(ncols);
                 cell = cmark_node_next(cell)) {

                std::string markup = "<span";
                if (is_header)
                    markup += " weight=\"bold\"";
                if (is_header || row_idx == 0)
                    markup += " underline=\"single\"";

                markup += ">";
                markup += renderInlinePango(cell);
                markup += "</span>";

                auto label = Gtk::Label::create(nullptr);
                label->set_wrap(true);
                label->set_xalign(0.0f);
                label->set_markup(markup.c_str());
                label->set_hexpand(true);

                float xalign = 0.0f;
                if (alignments && col_idx < static_cast<int>(ncols)) {
                    switch (alignments[col_idx]) {
                    case 'c': xalign = 0.5f; break;
                    case 'r': xalign = 1.0f; break;
                    default: xalign = 0.0f; break;
                    }
                }
                label->set_xalign(xalign);

                grid->attach(std::move(label).release_floating_ptr(),
                    col_idx, row_idx, 1, 1);
                col_idx++;
            }
            row_idx++;
        }
        return std::move(grid).template cast<Gtk::Widget>();
    }

    case CMARK_NODE_TASK_LIST_ITEM: {
        bool checked = cmark_gfm_extensions_get_tasklist_item_checked(node);

        auto row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 4);
        row->set_hexpand(true);

        auto cb = Gtk::CheckButton::create();
        cb->set_active(checked);
        cb->set_valign(Gtk::Align::START);
        cb->set_sensitive(false);
        cb->set_margin_top(1);
        row->append(std::move(cb));

        for (cmark_node* item_child = cmark_node_first_child(node);
             item_child; item_child = cmark_node_next(item_child)) {
            auto widget = renderBlock(item_child);
            if (widget) {
                widget->set_hexpand(true);
                row->append(std::move(widget));
            }
        }

        row->set_margin_start(12);
        return std::move(row).template cast<Gtk::Widget>();
    }

    case CMARK_NODE_THEMATIC_BREAK: {
        auto sep = Gtk::Separator::create(Gtk::Orientation::HORIZONTAL);
        sep->set_margin_top(8);
        sep->set_margin_bottom(8);
        return std::move(sep).template cast<Gtk::Widget>();
    }

    case CMARK_NODE_DOCUMENT: {
        auto box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 0);
        box->set_hexpand(true);

        for (cmark_node* child = cmark_node_first_child(node);
             child; child = cmark_node_next(child)) {
            auto widget = renderBlock(child);
            if (widget)
                box->append(std::move(widget));
        }
        return std::move(box).template cast<Gtk::Widget>();
    }

    default:
        return nullptr;
    }
}

FloatPtr<Gtk::Widget> MarkdownRenderer::render(const std::string& markdown)
{
    if (markdown.empty()) {
        auto label = Gtk::Label::create("");
        return std::move(label).template cast<Gtk::Widget>();
    }

    cmark_node* doc = nullptr;

    const char* ext_names[] = {
        "table", "strikethrough", "tasklist", "autolink", "tagfilter"
    };

    cmark_gfm_core_extensions_ensure_registered();

    cmark_parser* parser = cmark_parser_new(
        CMARK_OPT_DEFAULT | CMARK_OPT_VALIDATE_UTF8);

    for (const char* name : ext_names) {
        auto* ext = cmark_find_syntax_extension(name);
        if (ext)
            cmark_parser_attach_syntax_extension(parser, ext);
    }

    cmark_parser_feed(parser, markdown.c_str(), markdown.size());
    doc = cmark_parser_finish(parser);
    cmark_parser_free(parser);

    if (!doc) {
        auto label = Gtk::Label::create(markdown.c_str());
        label->set_wrap(true);
        return std::move(label).template cast<Gtk::Widget>();
    }

    auto widget = renderBlock(doc);
    cmark_node_free(doc);

    if (!widget) {
        auto label = Gtk::Label::create(markdown.c_str());
        label->set_wrap(true);
        return std::move(label).template cast<Gtk::Widget>();
    }

    return widget;
}

} // namespace rook::gui
