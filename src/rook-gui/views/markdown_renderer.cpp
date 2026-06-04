#include "markdown_renderer.hpp"
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <spdlog/spdlog.h>
#include <cstdlib>

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

GtkWidget* MarkdownRenderer::renderBlock(cmark_node* node)
{
    auto type = static_cast<int>(cmark_node_get_type(node));

    switch (type) {
    case CMARK_NODE_PARAGRAPH: {
        auto* label = gtk_label_new(nullptr);
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        auto markup = renderInlinePango(node);
        gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
        gtk_widget_set_margin_top(GTK_WIDGET(label), 2);
        gtk_widget_set_margin_bottom(GTK_WIDGET(label), 2);
        return GTK_WIDGET(label);
    }

    case CMARK_NODE_HEADING: {
        auto* label = gtk_label_new(nullptr);
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        int level = cmark_node_get_heading_level(node);
        std::string markup = "<span size=\"";
        markup += headingSize(level);
        markup += "\" weight=\"bold\">";
        markup += renderInlinePango(node);
        markup += "</span>";
        gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
        gtk_widget_set_margin_top(GTK_WIDGET(label), 6);
        gtk_widget_set_margin_bottom(GTK_WIDGET(label), 2);
        return GTK_WIDGET(label);
    }

    case CMARK_NODE_CODE_BLOCK: {
        const char* literal = cmark_node_get_literal(node);
        std::string code = literal ? std::string(literal) : "";

        auto* buf = gtk_text_buffer_new(nullptr);
        gtk_text_buffer_set_text(buf, code.c_str(), code.size());

        auto* tv = gtk_text_view_new_with_buffer(buf);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD);
        gtk_widget_set_margin_start(GTK_WIDGET(tv), kCodeBlockMargin);
        gtk_widget_set_margin_end(GTK_WIDGET(tv), kCodeBlockMargin);
        gtk_widget_set_margin_top(GTK_WIDGET(tv), kCodeBlockMargin);
        gtk_widget_set_margin_bottom(GTK_WIDGET(tv), kCodeBlockMargin);

        auto* frame = gtk_frame_new(nullptr);
        gtk_frame_set_child(GTK_FRAME(frame), GTK_WIDGET(tv));
        gtk_widget_add_css_class(GTK_WIDGET(frame), "code-block");
        gtk_widget_set_margin_top(GTK_WIDGET(frame), 4);
        gtk_widget_set_margin_bottom(GTK_WIDGET(frame), 4);
        gtk_widget_set_hexpand(GTK_WIDGET(frame), TRUE);

        return GTK_WIDGET(frame);
    }

    case CMARK_NODE_BLOCK_QUOTE: {
        auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(GTK_WIDGET(box), "blockquote");
        gtk_widget_set_margin_start(GTK_WIDGET(box), 6);
        gtk_widget_set_hexpand(GTK_WIDGET(box), TRUE);

        for (cmark_node* child = cmark_node_first_child(node);
             child; child = cmark_node_next(child)) {
            auto* widget = renderBlock(child);
            if (widget)
                gtk_box_append(GTK_BOX(box), widget);
        }
        return GTK_WIDGET(box);
    }

    case CMARK_NODE_LIST: {
        auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_margin_start(GTK_WIDGET(box), 12);

        cmark_list_type list_type = cmark_node_get_list_type(node);
        int n = static_cast<int>(cmark_node_get_list_start(node));

        for (cmark_node* item = cmark_node_first_child(node);
             item; item = cmark_node_next(item)) {

            auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            gtk_widget_set_hexpand(GTK_WIDGET(row), TRUE);

            char prefix[16];
            if (list_type == CMARK_ORDERED_LIST)
                std::snprintf(prefix, sizeof(prefix), "%d.", n++);
            else
                std::snprintf(prefix, sizeof(prefix), "\u2022");

            auto* bullet = gtk_label_new(prefix);
            gtk_widget_set_valign(GTK_WIDGET(bullet), GTK_ALIGN_START);
            gtk_widget_set_margin_top(GTK_WIDGET(bullet), 1);
            gtk_box_append(GTK_BOX(row), GTK_WIDGET(bullet));

            for (cmark_node* item_child = cmark_node_first_child(item);
                 item_child; item_child = cmark_node_next(item_child)) {
                auto* widget = renderBlock(item_child);
                if (widget) {
                    gtk_widget_set_hexpand(widget, TRUE);
                    gtk_box_append(GTK_BOX(row), widget);
                }
            }

            gtk_box_append(GTK_BOX(box), GTK_WIDGET(row));
        }
        return GTK_WIDGET(box);
    }

    case CMARK_NODE_TABLE: {
        uint16_t ncols = cmark_gfm_extensions_get_table_columns(node);
        if (ncols == 0) ncols = 1;
        auto* alignments = cmark_gfm_extensions_get_table_alignments(node);

        auto* grid = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
        gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
        gtk_widget_set_margin_top(GTK_WIDGET(grid), 4);
        gtk_widget_set_margin_bottom(GTK_WIDGET(grid), 4);
        gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);

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

                auto* label = gtk_label_new(nullptr);
                gtk_label_set_wrap(GTK_LABEL(label), TRUE);
                gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
                gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
                gtk_widget_set_hexpand(GTK_WIDGET(label), TRUE);

                float xalign = 0.0f;
                if (alignments && col_idx < static_cast<int>(ncols)) {
                    switch (alignments[col_idx]) {
                    case 'c': xalign = 0.5f; break;
                    case 'r': xalign = 1.0f; break;
                    default: xalign = 0.0f; break;
                    }
                }
                gtk_label_set_xalign(GTK_LABEL(label), xalign);

                gtk_grid_attach(GTK_GRID(grid),
                    GTK_WIDGET(label), col_idx, row_idx, 1, 1);
                col_idx++;
            }
            row_idx++;
        }
        return GTK_WIDGET(grid);
    }

    case CMARK_NODE_TASK_LIST_ITEM: {
        bool checked = cmark_gfm_extensions_get_tasklist_item_checked(node);

        auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_set_hexpand(GTK_WIDGET(row), TRUE);

        auto* cb = gtk_check_button_new();
        gtk_check_button_set_active(GTK_CHECK_BUTTON(cb), checked);
        gtk_widget_set_valign(GTK_WIDGET(cb), GTK_ALIGN_START);
        gtk_widget_set_sensitive(GTK_WIDGET(cb), FALSE);
        gtk_widget_set_margin_top(GTK_WIDGET(cb), 1);
        gtk_box_append(GTK_BOX(row), GTK_WIDGET(cb));

        for (cmark_node* item_child = cmark_node_first_child(node);
             item_child; item_child = cmark_node_next(item_child)) {
            auto* widget = renderBlock(item_child);
            if (widget) {
                gtk_widget_set_hexpand(widget, TRUE);
                gtk_box_append(GTK_BOX(row), widget);
            }
        }

        gtk_widget_set_margin_start(GTK_WIDGET(row), 12);
        return GTK_WIDGET(row);
    }

    case CMARK_NODE_THEMATIC_BREAK: {
        auto* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_margin_top(GTK_WIDGET(sep), 8);
        gtk_widget_set_margin_bottom(GTK_WIDGET(sep), 8);
        return GTK_WIDGET(sep);
    }

    case CMARK_NODE_DOCUMENT: {
        auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand(GTK_WIDGET(box), TRUE);

        for (cmark_node* child = cmark_node_first_child(node);
             child; child = cmark_node_next(child)) {
            auto* widget = renderBlock(child);
            if (widget)
                gtk_box_append(GTK_BOX(box), widget);
        }
        return GTK_WIDGET(box);
    }

    default:
        return nullptr;
    }
}

GtkWidget* MarkdownRenderer::render(const std::string& markdown)
{
    if (markdown.empty())
        return gtk_label_new("");

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
        auto* label = gtk_label_new(markdown.c_str());
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        return GTK_WIDGET(label);
    }

    auto* widget = renderBlock(doc);
    cmark_node_free(doc);

    if (!widget) {
        auto* label = gtk_label_new(markdown.c_str());
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        return GTK_WIDGET(label);
    }

    return widget;
}

} // namespace rook::gui
