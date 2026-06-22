#include <draxul/markdown/markdown_layout.h>

#include <draxul/unicode.h>

#include <algorithm>
#include <cctype>
#include <numeric>
#include <sstream>
#include <string_view>
#include <utility>

namespace draxul::markdown
{
namespace
{

constexpr float kBlockGap = 8.0f;
constexpr float kListBlockIndent = 10.0f;
constexpr float kListMarkerWidth = 18.0f;
constexpr float kQuoteInset = 16.0f;
constexpr float kTableCellPaddingX = 8.0f;
constexpr float kSectionIndentStep = 12.0f;
constexpr float kSectionMaxIndent = 48.0f;
constexpr float kTableMinColumnWidth = 84.0f;
constexpr float kTableMaxPreferredColumnShare = 0.55f;

constexpr StyleId kBodyStyle{ 0 };
constexpr StyleId kHeading1Style{ 1 };
constexpr StyleId kHeading2Style{ 2 };
constexpr StyleId kHeading3Style{ 3 };
constexpr StyleId kHeading4Style{ 4 };
constexpr StyleId kHeading5Style{ 5 };
constexpr StyleId kHeading6Style{ 6 };
constexpr StyleId kCodeStyle{ 7 };

const MarkdownTextStyle& style_for_id(const MarkdownTheme& theme, StyleId style)
{
    switch (style.value)
    {
    case kHeading1Style.value:
        return theme.heading1;
    case kHeading2Style.value:
        return theme.heading2;
    case kHeading3Style.value:
        return theme.heading3;
    case kHeading4Style.value:
        return theme.heading4;
    case kHeading5Style.value:
        return theme.heading5;
    case kHeading6Style.value:
        return theme.heading6;
    case kCodeStyle.value:
        return theme.code;
    default:
        return theme.body;
    }
}

StyleId style_id_for_heading(int level)
{
    switch (level)
    {
    case 1:
        return kHeading1Style;
    case 2:
        return kHeading2Style;
    case 3:
        return kHeading3Style;
    case 4:
        return kHeading4Style;
    case 5:
        return kHeading5Style;
    case 6:
        return kHeading6Style;
    default:
        return kHeading6Style;
    }
}

std::string collect_inline_text(const std::vector<Inline>& inlines)
{
    std::string text;
    for (const auto& inline_node : inlines)
    {
        if (!inline_node.text.empty())
            text += inline_node.text;
        if (!inline_node.children.empty())
            text += collect_inline_text(inline_node.children);
        if (inline_node.kind == InlineKind::SoftBreak || inline_node.kind == InlineKind::LineBreak)
            text += '\n';
    }
    return text;
}

std::vector<std::string> split_preserving_lines(std::string_view text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t newline = text.find('\n', start);
        if (newline == std::string_view::npos)
        {
            lines.emplace_back(text.substr(start));
            break;
        }

        lines.emplace_back(text.substr(start, newline - start));
        start = newline + 1;
    }
    return lines;
}

std::vector<std::string> split_words(std::string_view text)
{
    std::vector<std::string> words;
    size_t index = 0;
    while (index < text.size())
    {
        while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])))
            ++index;
        const size_t start = index;
        while (index < text.size() && !std::isspace(static_cast<unsigned char>(text[index])))
            ++index;
        if (start < index)
            words.emplace_back(text.substr(start, index - start));
    }
    return words;
}

class LayoutBuilder
{
public:
    LayoutBuilder(
        const MarkdownTheme& theme,
        draxul::RichTextService& rich_text,
        const LayoutOptions& options)
        : theme_(theme)
        , rich_text_(rich_text)
        , scale_(std::max(0.01f, options.pixel_scale))
        , margin_columns_(std::max(0.0f, options.margin_columns))
    {
        document_.content_width = std::max(1.0f, options.viewport_width - 2.0f * padding());
    }

    LayoutDocument finish()
    {
        document_.content_height = y_;
        return std::move(document_);
    }

    void layout_blocks(const std::vector<Block>& blocks, float indent = 0.0f)
    {
        layout_document_blocks(blocks, indent);
    }

    void layout_block(const Block& block, float indent = 0.0f)
    {
        switch (block.kind)
        {
        case BlockKind::FrontMatter:
            layout_front_matter(block, indent);
            break;
        case BlockKind::Heading:
            layout_wrapped_text(
                collect_inline_text(block.inlines),
                style_id_for_heading(block.heading_level),
                block.kind,
                block.source,
                indent,
                {});
            add_block_gap();
            break;
        case BlockKind::Paragraph:
            layout_wrapped_text(
                collect_inline_text(block.inlines),
                kBodyStyle,
                block.kind,
                block.source,
                indent,
                {});
            add_block_gap();
            break;
        case BlockKind::CodeBlock:
        case BlockKind::HtmlBlock:
            layout_code_block(block, indent);
            add_block_gap();
            break;
        case BlockKind::List:
            for (const auto& child : block.children)
                layout_list_item(child, indent + kListBlockIndent * scale_, block.ordered);
            add_block_gap();
            break;
        case BlockKind::ListItem:
        case BlockKind::TaskItem:
            layout_list_item(block, indent, false);
            add_block_gap();
            break;
        case BlockKind::BlockQuote:
        case BlockKind::Callout:
            layout_quote_like(block, indent);
            add_block_gap();
            break;
        case BlockKind::ThematicBreak:
            layout_divider(block, indent);
            add_block_gap();
            break;
        case BlockKind::Table:
        case BlockKind::TableRow:
        case BlockKind::TableCell:
            layout_table_like(block, indent);
            add_block_gap();
            break;
        case BlockKind::Document:
            layout_document_blocks(block.children, indent);
            break;
        }
    }

private:
    float padding() const
    {
        const auto& metrics = rich_text_.metrics_for(theme_.body.rich_text);
        return std::max(0.0f, static_cast<float>(metrics.cell_width) * margin_columns_ * scale_);
    }

    float block_gap() const
    {
        return kBlockGap * scale_;
    }

    float content_left(float indent) const
    {
        return padding() + indent;
    }

    float available_width(float indent) const
    {
        return std::max(1.0f, document_.content_width - indent);
    }

    float table_cell_padding_x() const
    {
        return kTableCellPaddingX * scale_;
    }

    float table_border_thickness() const
    {
        return std::max(1.0f, scale_);
    }

    float section_indent_for_heading(int heading_level) const
    {
        if (heading_level <= 1)
            return 0.0f;
        return std::min(kSectionMaxIndent * scale_, static_cast<float>(heading_level - 1) * kSectionIndentStep * scale_);
    }

    draxul::Color table_row_background(bool header, int body_row_index) const
    {
        if (header)
            return theme_.panel_background;

        const auto& base = (body_row_index % 2 == 0) ? theme_.document_background : theme_.code_background;
        const float alpha = (body_row_index % 2 == 0) ? 0.12f : 0.32f;
        return draxul::Color(base.r, base.g, base.b, alpha);
    }

    float measure(std::string_view text, StyleId style)
    {
        if (text.empty())
            return 0.0f;
        const auto& markdown_style = style_for_id(theme_, style);
        const auto& metrics = rich_text_.metrics_for(markdown_style.rich_text);
        const float cell_width = static_cast<float>(std::max(1, metrics.cell_width));

        float width = 0.0f;
        size_t offset = 0;
        while (offset < text.size())
        {
            const size_t cluster_start = offset;
            uint32_t cp = 0;
            if (!draxul::utf8_decode_next(text, offset, cp))
                break;

            if (cp == '\t')
            {
                width += cell_width * 4.0f;
                continue;
            }

            if (cp == '\r' || cp == '\n' || draxul::is_width_ignorable(cp) || draxul::is_emoji_modifier(cp))
                continue;

            width += cell_width
                * static_cast<float>(draxul::cluster_cell_width(text.substr(cluster_start, offset - cluster_start)));
        }

        return width;
    }

    float row_height_for(StyleId style)
    {
        const auto& markdown_style = style_for_id(theme_, style);
        const auto& metrics = rich_text_.metrics_for(markdown_style.rich_text);
        return std::max(1.0f, static_cast<float>(metrics.cell_height) * markdown_style.line_height_multiplier);
    }

    float baseline_for(StyleId style, float row_y, float row_height)
    {
        const auto& markdown_style = style_for_id(theme_, style);
        const auto& metrics = rich_text_.metrics_for(markdown_style.rich_text);
        const float leading = std::max(0.0f, row_height - static_cast<float>(metrics.cell_height));
        return row_y + leading * 0.5f + static_cast<float>(metrics.ascender);
    }

    void add_block_gap()
    {
        if (!document_.rows.empty())
            y_ += block_gap();
    }

    LayoutRow& append_text_row(
        std::string text,
        StyleId style,
        BlockKind source_kind,
        SourceSpan source,
        float indent,
        std::vector<Decoration> decorations)
    {
        const float row_height = row_height_for(style);
        const float baseline = baseline_for(style, y_, row_height);

        LayoutRow row;
        row.y = y_;
        row.height = row_height;
        row.baseline = baseline;
        row.source_kind = source_kind;
        row.source = source;
        row.decorations = std::move(decorations);
        if (!text.empty())
        {
            row.runs.push_back(TextRun{
                .text = std::move(text),
                .style = style,
                .x = content_left(indent),
                .baseline = baseline,
                .source = source,
            });
        }

        document_.rows.push_back(std::move(row));
        y_ += row_height;
        return document_.rows.back();
    }

    std::vector<std::string> wrap_line(std::string_view text, StyleId style, float width)
    {
        std::vector<std::string> rows;
        auto words = split_words(text);
        if (words.empty())
        {
            rows.emplace_back();
            return rows;
        }

        std::string current;
        auto push_current = [&rows, &current]() {
            if (current.empty())
                return;
            rows.push_back(std::exchange(current, {}));
        };

        auto append_cluster = [this, style, width, &current, &push_current](std::string_view cluster) {
            if (cluster.empty())
                return;

            const std::string candidate = current + std::string(cluster);
            if (!current.empty() && measure(candidate, style) > width)
                push_current();
            current.append(cluster);
        };

        auto append_long_word = [&append_cluster](std::string_view word) {
            size_t offset = 0;
            while (offset < word.size())
            {
                const size_t cluster_start = offset;
                uint32_t cp = 0;
                if (!draxul::utf8_decode_next(word, offset, cp))
                    break;
                append_cluster(word.substr(cluster_start, offset - cluster_start));
            }
        };

        for (const auto& word : words)
        {
            const std::string candidate = current.empty() ? word : current + " " + word;
            if (measure(candidate, style) <= width)
            {
                current = candidate;
                continue;
            }

            push_current();
            if (measure(word, style) <= width)
                current = word;
            else
                append_long_word(word);
        }

        push_current();
        if (rows.empty())
            rows.emplace_back();
        return rows;
    }

    void layout_wrapped_text(
        std::string_view text,
        StyleId style,
        BlockKind source_kind,
        SourceSpan source,
        float indent,
        std::vector<Decoration> row_decorations)
    {
        for (const auto& raw_line : split_preserving_lines(text))
        {
            auto wrapped = wrap_line(raw_line, style, available_width(indent));
            for (auto& line : wrapped)
                append_text_row(std::move(line), style, source_kind, source, indent, row_decorations);
        }
    }

    void layout_document_blocks(const std::vector<Block>& blocks, float indent)
    {
        float section_indent = 0.0f;
        for (const auto& child : blocks)
        {
            if (child.kind == BlockKind::Heading)
                section_indent = section_indent_for_heading(child.heading_level);
            layout_block(child, indent + section_indent);
        }
    }

    void layout_front_matter(const Block& block, float indent)
    {
        if (block.front_matter.empty())
        {
            layout_decorated_single_line("metadata", kBodyStyle, block.kind, block.source, indent, theme_.panel_background);
            return;
        }

        for (const auto& entry : block.front_matter)
        {
            layout_decorated_single_line(
                entry.key + ": " + entry.value,
                kBodyStyle,
                block.kind,
                block.source,
                indent,
                theme_.panel_background);
        }
        add_block_gap();
    }

    void layout_code_block(const Block& block, float indent)
    {
        for (auto& line : split_preserving_lines(block.literal))
        {
            layout_decorated_single_line(
                line,
                kCodeStyle,
                block.kind,
                block.source,
                indent,
                theme_.code_background);
        }
    }

    void layout_decorated_single_line(
        std::string text,
        StyleId style,
        BlockKind source_kind,
        SourceSpan source,
        float indent,
        draxul::Color color)
    {
        const float height = row_height_for(style);
        std::vector<Decoration> decorations;
        decorations.push_back(Decoration{
            .kind = Decoration::Kind::Background,
            .x = content_left(indent) - 8.0f * scale_,
            .y = y_,
            .width = available_width(indent) + 16.0f * scale_,
            .height = height,
            .color = color,
        });
        append_text_row(std::move(text), style, source_kind, source, indent, std::move(decorations));
    }

    void layout_list_item(const Block& block, float indent, bool ordered)
    {
        const float item_indent = indent + kListMarkerWidth * scale_;
        const size_t first_row = document_.rows.size();

        if (!block.inlines.empty())
        {
            layout_wrapped_text(
                collect_inline_text(block.inlines),
                kBodyStyle,
                block.kind,
                block.source,
                item_indent,
                {});
        }

        for (const auto& child : block.children)
            layout_block(child, item_indent);

        if (document_.rows.size() == first_row)
            append_text_row({}, kBodyStyle, block.kind, block.source, item_indent, {});

        auto& first = document_.rows[first_row];
        first.source_kind = block.kind;
        const auto kind = block.kind == BlockKind::TaskItem ? Decoration::Kind::Checkbox : Decoration::Kind::Bullet;
        first.decorations.push_back(Decoration{
            .kind = kind,
            .x = content_left(indent),
            .y = first.y + first.height * 0.35f,
            .width = ordered ? 10.0f * scale_ : 8.0f * scale_,
            .height = 8.0f * scale_,
            .color = theme_.accent,
            .checked = block.checked,
        });
    }

    void layout_quote_like(const Block& block, float indent)
    {
        const size_t first_row = document_.rows.size();
        if (block.kind == BlockKind::Callout && !block.callout_title.empty())
            layout_wrapped_text(block.callout_title, kHeading6Style, block.kind, block.source, indent + kQuoteInset, {});

        for (const auto& child : block.children)
            layout_block(child, indent + kQuoteInset);

        for (size_t index = first_row; index < document_.rows.size(); ++index)
        {
            auto& row = document_.rows[index];
            row.decorations.push_back(Decoration{
                .kind = Decoration::Kind::Background,
                .x = content_left(indent) - 6.0f * scale_,
                .y = row.y,
                .width = available_width(indent) + 12.0f * scale_,
                .height = row.height,
                .color = theme_.panel_background,
            });
            row.decorations.push_back(Decoration{
                .kind = Decoration::Kind::BorderLeft,
                .x = content_left(indent),
                .y = row.y,
                .width = 3.0f * scale_,
                .height = row.height,
                .color = theme_.accent,
            });
            if (row.source_kind == BlockKind::Paragraph)
                row.source_kind = block.kind;
        }
    }

    void layout_divider(const Block& block, float indent)
    {
        const float height = 12.0f * scale_;
        LayoutRow row;
        row.y = y_;
        row.height = height;
        row.baseline = y_ + height * 0.5f;
        row.source_kind = block.kind;
        row.source = block.source;
        row.decorations.push_back(Decoration{
            .kind = Decoration::Kind::Divider,
            .x = content_left(indent),
            .y = y_ + height * 0.5f,
            .width = available_width(indent),
            .height = 1.0f * scale_,
            .color = theme_.border,
        });
        document_.rows.push_back(std::move(row));
        y_ += height;
    }

    int table_column_count(const Block& table) const
    {
        int column_count = std::max(0, table.table_column_count);
        for (const auto& row : table.children)
        {
            if (row.kind != BlockKind::TableRow)
                continue;
            column_count = std::max(column_count, static_cast<int>(row.children.size()));
        }
        return column_count;
    }

    bool table_row_is_header(const Block& row) const
    {
        if (row.table_header)
            return true;
        return std::ranges::any_of(row.children, [](const Block& cell) {
            return cell.kind == BlockKind::TableCell && cell.table_header;
        });
    }

    float longest_word_width(std::string_view text, StyleId style)
    {
        float width = 0.0f;
        for (const auto& word : split_words(text))
            width = std::max(width, measure(word, style));
        return width;
    }

    struct TableColumnWidthBudget
    {
        float minimum = 0.0f;
        float preferred = 0.0f;
    };

    std::vector<float> table_column_widths(const Block& table, float table_width, int column_count)
    {
        std::vector<float> widths(static_cast<size_t>(column_count), 0.0f);
        if (column_count <= 0)
            return widths;

        const float edge_width = table_cell_padding_x() * 2.0f + table_border_thickness() * 2.0f;
        const float readable_minimum = kTableMinColumnWidth * scale_;
        std::vector<TableColumnWidthBudget> budgets(static_cast<size_t>(column_count));
        for (auto& budget : budgets)
        {
            budget.minimum = readable_minimum;
            budget.preferred = readable_minimum;
        }

        for (const auto& row : table.children)
        {
            if (row.kind != BlockKind::TableRow)
                continue;

            const bool header = table_row_is_header(row);
            const StyleId style = header ? kHeading6Style : kBodyStyle;
            for (size_t column = 0; column < row.children.size() && column < widths.size(); ++column)
            {
                const auto& cell = row.children[column];
                if (cell.kind != BlockKind::TableCell)
                    continue;

                const std::string text = collect_inline_text(cell.inlines);
                const float full_text_width = measure(text, style) + edge_width;
                const float min_text_width = (header ? std::max(full_text_width, longest_word_width(text, style) + edge_width)
                                                      : longest_word_width(text, style) + edge_width);
                auto& budget = budgets[column];
                budget.minimum = std::max(budget.minimum, min_text_width);
                budget.preferred = std::max(budget.preferred, full_text_width);
            }
        }

        const float max_preferred_width = column_count == 1
            ? table_width
            : std::max(1.0f, table_width * kTableMaxPreferredColumnShare);
        for (auto& budget : budgets)
        {
            budget.preferred = std::max(budget.minimum, std::min(budget.preferred, max_preferred_width));
        }

        for (size_t index = 0; index < budgets.size(); ++index)
            widths[index] = budgets[index].minimum;

        const float min_total = std::accumulate(widths.begin(), widths.end(), 0.0f);
        if (min_total >= table_width)
        {
            const float shrink_ratio = table_width / std::max(1.0f, min_total);
            for (auto& width : widths)
                width = std::max(0.0f, width * shrink_ratio);
            widths.back() += table_width - std::accumulate(widths.begin(), widths.end(), 0.0f);
            return widths;
        }

        std::vector<float> preferred_widths(widths.size(), 0.0f);
        for (size_t index = 0; index < budgets.size(); ++index)
            preferred_widths[index] = budgets[index].preferred;

        const float preferred_total = std::accumulate(preferred_widths.begin(), preferred_widths.end(), 0.0f);
        if (preferred_total >= table_width)
        {
            const float growth_capacity = std::max(1.0f, preferred_total - min_total);
            const float growth_ratio = std::min(1.0f, (table_width - min_total) / growth_capacity);
            for (size_t index = 0; index < widths.size(); ++index)
                widths[index] += (preferred_widths[index] - widths[index]) * growth_ratio;
            widths.back() += table_width - std::accumulate(widths.begin(), widths.end(), 0.0f);
            return widths;
        }

        widths = std::move(preferred_widths);
        float extra = table_width - preferred_total;
        while (extra > 0.01f)
        {
            std::vector<size_t> expandable;
            for (size_t index = 0; index < widths.size(); ++index)
            {
                if (widths[index] + 0.01f < max_preferred_width)
                    expandable.push_back(index);
            }

            if (expandable.empty())
                break;

            const float share = extra / static_cast<float>(expandable.size());
            float used = 0.0f;
            for (size_t index : expandable)
            {
                const float room = max_preferred_width - widths[index];
                const float addition = std::min(room, share);
                widths[index] += addition;
                used += addition;
            }

            if (used <= 0.0f)
                break;
            extra -= used;
        }

        if (extra > 0.0f)
        {
            const float equal_extra = extra / static_cast<float>(column_count);
            for (auto& width : widths)
                width += equal_extra;
        }

        widths.back() += table_width - std::accumulate(widths.begin(), widths.end(), 0.0f);
        return widths;
    }

    TableCellAlignment effective_alignment(TableCellAlignment alignment) const
    {
        return alignment == TableCellAlignment::Default ? TableCellAlignment::Left : alignment;
    }

    float aligned_cell_text_x(
        float cell_x,
        float cell_width,
        std::string_view line,
        StyleId style,
        TableCellAlignment alignment)
    {
        const float padding_x = table_cell_padding_x();
        const float inner_width = std::max(1.0f, cell_width - padding_x * 2.0f);
        const float text_width = measure(line, style);

        switch (effective_alignment(alignment))
        {
        case TableCellAlignment::Center:
            return cell_x + padding_x + std::max(0.0f, (inner_width - text_width) * 0.5f);
        case TableCellAlignment::Right:
            return cell_x + padding_x + std::max(0.0f, inner_width - text_width);
        case TableCellAlignment::Default:
        case TableCellAlignment::Left:
        default:
            return cell_x + padding_x;
        }
    }

    void add_table_row_decorations(
        LayoutRow& visual_row,
        const std::vector<float>& column_widths,
        float table_x,
        bool header,
        int body_row_index,
        bool first_table_row,
        bool first_visual_line,
        bool last_visual_line)
    {
        const float thickness = table_border_thickness();
        float x = table_x;
        const auto background = table_row_background(header, body_row_index);
        for (float column_width : column_widths)
        {
            visual_row.decorations.push_back(Decoration{
                .kind = Decoration::Kind::TableCellBackground,
                .x = x,
                .y = visual_row.y,
                .width = column_width,
                .height = visual_row.height,
                .color = background,
            });
            visual_row.decorations.push_back(Decoration{
                .kind = Decoration::Kind::TableBorder,
                .x = x,
                .y = visual_row.y,
                .width = thickness,
                .height = visual_row.height,
                .color = theme_.border,
            });
            x += column_width;
        }
        visual_row.decorations.push_back(Decoration{
            .kind = Decoration::Kind::TableBorder,
            .x = x - thickness,
            .y = visual_row.y,
            .width = thickness,
            .height = visual_row.height,
            .color = theme_.border,
        });

        if ((first_table_row && first_visual_line) || last_visual_line)
        {
            visual_row.decorations.push_back(Decoration{
                .kind = Decoration::Kind::TableBorder,
                .x = table_x,
                .y = last_visual_line ? visual_row.y + visual_row.height - thickness : visual_row.y,
                .width = std::max(1.0f, x - table_x),
                .height = thickness,
                .color = theme_.border,
            });
        }
    }

    void layout_table_like(const Block& block, float indent)
    {
        if (block.kind != BlockKind::Table)
        {
            for (const auto& child : block.children)
                layout_table_like(child, indent);
            if (block.children.empty())
                layout_wrapped_text(collect_inline_text(block.inlines), kBodyStyle, block.kind, block.source, indent, {});
            return;
        }

        const int column_count = table_column_count(block);
        if (column_count <= 0)
            return;

        const float table_x = content_left(indent);
        const float table_width = available_width(indent);
        const auto column_widths = table_column_widths(block, table_width, column_count);
        const float padding_x = table_cell_padding_x();

        bool first_table_row = true;
        int body_row_index = 0;
        for (const auto& row : block.children)
        {
            if (row.kind != BlockKind::TableRow)
                continue;

            const bool header = table_row_is_header(row);
            const StyleId style = header ? kHeading6Style : kBodyStyle;
            const float row_height = row_height_for(style);
            std::vector<std::vector<std::string>> wrapped_cells(static_cast<size_t>(column_count));
            size_t visual_line_count = 1;

            for (int column = 0; column < column_count; ++column)
            {
                std::string text;
                if (column < static_cast<int>(row.children.size()) && row.children[column].kind == BlockKind::TableCell)
                    text = collect_inline_text(row.children[column].inlines);

                const float inner_width = std::max(1.0f, column_widths[static_cast<size_t>(column)] - padding_x * 2.0f);
                wrapped_cells[static_cast<size_t>(column)] = wrap_line(text, style, inner_width);
                visual_line_count = std::max(visual_line_count, wrapped_cells[static_cast<size_t>(column)].size());
            }

            for (size_t visual_line = 0; visual_line < visual_line_count; ++visual_line)
            {
                LayoutRow visual_row;
                visual_row.y = y_;
                visual_row.height = row_height;
                visual_row.baseline = baseline_for(style, y_, row_height);
                visual_row.source_kind = BlockKind::TableRow;
                visual_row.source = row.source;

                add_table_row_decorations(
                    visual_row,
                    column_widths,
                    table_x,
                    header,
                    body_row_index,
                    first_table_row,
                    visual_line == 0,
                    visual_line + 1 == visual_line_count);

                float cell_x = table_x;
                for (int column = 0; column < column_count; ++column)
                {
                    const auto& lines = wrapped_cells[static_cast<size_t>(column)];
                    const std::string line = visual_line < lines.size() ? lines[visual_line] : std::string();
                    if (!line.empty())
                    {
                        const Block* cell = column < static_cast<int>(row.children.size()) ? &row.children[column] : nullptr;
                        const TableCellAlignment alignment = cell != nullptr
                            ? cell->table_alignment
                            : TableCellAlignment::Default;
                        visual_row.runs.push_back(TextRun{
                            .text = line,
                            .style = style,
                            .x = aligned_cell_text_x(
                                cell_x,
                                column_widths[static_cast<size_t>(column)],
                                line,
                                style,
                                alignment),
                            .baseline = visual_row.baseline,
                            .source = cell != nullptr ? cell->source : row.source,
                        });
                    }
                    cell_x += column_widths[static_cast<size_t>(column)];
                }

                document_.rows.push_back(std::move(visual_row));
                y_ += row_height;
            }

            if (!header)
                ++body_row_index;
            first_table_row = false;
        }
    }

    const MarkdownTheme& theme_;
    draxul::RichTextService& rich_text_;
    float scale_ = 1.0f;
    float margin_columns_ = 2.0f;
    float y_ = 0.0f;
    LayoutDocument document_;
};

} // namespace

LayoutDocument layout_markdown_document(
    const Document& document,
    const MarkdownTheme& theme,
    draxul::RichTextService& rich_text,
    const LayoutOptions& options)
{
    LayoutBuilder builder(theme, rich_text, options);
    builder.layout_blocks(document.blocks);
    return builder.finish();
}

VisibleRowRange visible_rows(
    const LayoutDocument& document,
    float scroll_offset,
    float viewport_height)
{
    if (document.rows.empty() || viewport_height <= 0.0f)
        return {};

    const float visible_start = std::max(0.0f, scroll_offset);
    const float visible_end = visible_start + viewport_height;

    const auto first_it = std::ranges::upper_bound(
        document.rows,
        visible_start,
        {},
        [](const LayoutRow& row) { return row.y + row.height; });

    if (first_it == document.rows.end())
        return { document.rows.size(), 0 };

    const size_t first = static_cast<size_t>(std::distance(document.rows.begin(), first_it));
    size_t count = 0;
    for (size_t index = first; index < document.rows.size(); ++index)
    {
        const auto& row = document.rows[index];
        if (row.y >= visible_end)
            break;
        ++count;
    }

    return { first, count };
}

} // namespace draxul::markdown
