#include "support/test_support.h"

#include <catch2/catch_all.hpp>

#include <draxul/markdown/markdown_document.h>
#include <draxul/markdown/markdown_layout.h>
#include <draxul/markdown/markdown_parser.h>
#include <draxul/markdown/markdown_theme.h>
#include <draxul/rich_text_service.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::markdown;

namespace
{

TextServiceConfig rich_text_test_config()
{
    TextServiceConfig config;
    config.font_path = (draxul::tests::project_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf").string();
    return config;
}

RichTextService make_initialized_service(float base_point_size = 12.0f)
{
    auto config = rich_text_test_config();
    REQUIRE(std::filesystem::exists(config.font_path));

    RichTextService service;
    REQUIRE(service.initialize(config, base_point_size, 96.0f));
    return service;
}

Inline text_inline(std::string text)
{
    Inline inline_node;
    inline_node.kind = InlineKind::Text;
    inline_node.text = std::move(text);
    return inline_node;
}

Block paragraph(std::string text)
{
    Block block;
    block.kind = BlockKind::Paragraph;
    block.inlines.push_back(text_inline(std::move(text)));
    return block;
}

Block heading(int level, std::string text)
{
    Block block;
    block.kind = BlockKind::Heading;
    block.heading_level = level;
    block.inlines.push_back(text_inline(std::move(text)));
    return block;
}

Inline break_inline(InlineKind kind)
{
    Inline inline_node;
    inline_node.kind = kind;
    inline_node.text = "\n"; // matches what the md4c parser stores on break nodes
    return inline_node;
}

Inline emphasis_inline(InlineKind kind, std::string text)
{
    Inline inline_node;
    inline_node.kind = kind;
    inline_node.children.push_back(text_inline(std::move(text)));
    return inline_node;
}

Block paragraph_of(std::vector<Inline> inlines)
{
    Block block;
    block.kind = BlockKind::Paragraph;
    block.inlines = std::move(inlines);
    return block;
}

Block task_item(std::string text, bool checked)
{
    Block item;
    item.kind = BlockKind::TaskItem;
    item.checked = checked;
    item.inlines.push_back(text_inline(std::move(text)));

    Block list;
    list.kind = BlockKind::List;
    list.children.push_back(std::move(item));
    return list;
}

Block table_cell(std::string text, TableCellAlignment alignment = TableCellAlignment::Default)
{
    Block block;
    block.kind = BlockKind::TableCell;
    block.table_alignment = alignment;
    block.inlines.push_back(text_inline(std::move(text)));
    return block;
}

Block table_row(std::vector<Block> cells, bool header = false)
{
    Block block;
    block.kind = BlockKind::TableRow;
    block.table_header = header;
    block.children = std::move(cells);
    for (auto& cell : block.children)
        cell.table_header = header;
    return block;
}

std::vector<float> table_background_widths(const LayoutRow& row)
{
    std::vector<float> widths;
    for (const auto& decoration : row.decorations)
    {
        if (decoration.kind == Decoration::Kind::TableCellBackground)
            widths.push_back(decoration.width);
    }
    return widths;
}

} // namespace

TEST_CASE("default markdown theme scales headings above body text", "[markdown][layout]")
{
    const auto theme = default_markdown_theme(12.0f);

    REQUIRE(theme.heading1.rich_text.point_size > theme.heading2.rich_text.point_size);
    REQUIRE(theme.heading2.rich_text.point_size > theme.body.rich_text.point_size);
    REQUIRE(theme.heading1.rich_text.point_size == Catch::Approx(theme.body.rich_text.point_size * 1.40f));
    REQUIRE(theme.heading2.rich_text.point_size == Catch::Approx(theme.body.rich_text.point_size * 1.20f));
    REQUIRE(theme.heading3.rich_text.point_size == Catch::Approx(theme.body.rich_text.point_size * 1.06f));
    REQUIRE(theme.heading4.rich_text.point_size == Catch::Approx(theme.body.rich_text.point_size));
    REQUIRE(theme.heading5.rich_text.point_size == Catch::Approx(theme.body.rich_text.point_size));
    REQUIRE(theme.heading6.rich_text.point_size == Catch::Approx(theme.body.rich_text.point_size));
    REQUIRE(style_for_heading(theme, 1).rich_text.point_size == theme.heading1.rich_text.point_size);
}

TEST_CASE("markdown layout gives H1 a taller row than body text", "[markdown][layout]")
{
    Document document;
    document.blocks.push_back(heading(1, "Large title"));
    document.blocks.push_back(paragraph("Small body"));

    auto service = make_initialized_service();
    const auto layout = layout_markdown_document(
        document,
        default_markdown_theme(12.0f),
        service,
        LayoutOptions{ .viewport_width = 600.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() == 2);
    REQUIRE(layout.rows[0].source_kind == BlockKind::Heading);
    REQUIRE(layout.rows[1].source_kind == BlockKind::Paragraph);
    REQUIRE(layout.rows[0].height > layout.rows[1].height);

    service.shutdown();
}

TEST_CASE("markdown layout left margin is measured in body character widths", "[markdown][layout]")
{
    Document document;
    document.blocks.push_back(paragraph("Margin controlled body text."));

    auto service = make_initialized_service();
    const auto theme = default_markdown_theme(12.0f);
    const auto metrics = service.metrics_for(theme.body.rich_text);

    const auto layout = layout_markdown_document(
        document,
        theme,
        service,
        LayoutOptions{
            .viewport_width = 600.0f,
            .viewport_height = 400.0f,
            .pixel_scale = 1.0f,
            .margin_columns = 2.0f,
        });

    REQUIRE(layout.rows.size() == 1);
    REQUIRE(layout.rows[0].runs.front().x == Catch::Approx(static_cast<float>(metrics.cell_width) * 2.0f));

    service.shutdown();
}

TEST_CASE("markdown layout wraps long paragraphs at narrow content width", "[markdown][layout]")
{
    Document document;
    document.blocks.push_back(paragraph(
        "This paragraph contains enough words to wrap into several visual rows when the viewport is narrow."));

    auto service = make_initialized_service();
    const auto layout = layout_markdown_document(
        document,
        default_markdown_theme(12.0f),
        service,
        LayoutOptions{ .viewport_width = 160.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() > 1);
    for (const auto& row : layout.rows)
    {
        REQUIRE(row.source_kind == BlockKind::Paragraph);
        REQUIRE(row.height > 0.0f);
    }

    service.shutdown();
}

TEST_CASE("markdown layout splits long unbroken words without rasterizing text", "[markdown][layout]")
{
    Document document;
    document.blocks.push_back(paragraph(std::string(240, 'w')));

    auto service = make_initialized_service();
    service.clear_atlas_dirty();

    const auto layout = layout_markdown_document(
        document,
        default_markdown_theme(12.0f),
        service,
        LayoutOptions{ .viewport_width = 160.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() > 1);
    for (const auto& row : layout.rows)
    {
        REQUIRE(row.runs.size() == 1);
        REQUIRE(row.runs.front().text.size() < 240);
    }
    REQUIRE_FALSE(service.atlas_dirty());

    service.shutdown();
}

TEST_CASE("markdown layout indents subsection headings and content", "[markdown][layout]")
{
    Document document;
    document.blocks.push_back(heading(1, "Project"));
    document.blocks.push_back(paragraph("Top level summary."));
    document.blocks.push_back(heading(2, "Rendering"));
    document.blocks.push_back(paragraph("Rendering details."));
    document.blocks.push_back(heading(3, "Tables"));
    document.blocks.push_back(paragraph("Table details."));
    document.blocks.push_back(heading(2, "Input"));
    document.blocks.push_back(paragraph("Input details."));

    auto service = make_initialized_service();
    const auto layout = layout_markdown_document(
        document,
        default_markdown_theme(12.0f),
        service,
        LayoutOptions{ .viewport_width = 720.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() == 8);

    const float top_level_x = layout.rows[1].runs.front().x;
    const float h2_x = layout.rows[2].runs.front().x;
    const float h2_body_x = layout.rows[3].runs.front().x;
    const float h3_x = layout.rows[4].runs.front().x;
    const float h3_body_x = layout.rows[5].runs.front().x;
    const float next_h2_x = layout.rows[6].runs.front().x;

    REQUIRE(h2_x > top_level_x);
    REQUIRE(h2_body_x == Catch::Approx(h2_x));
    REQUIRE(h3_x > h2_x);
    REQUIRE(h3_body_x == Catch::Approx(h3_x));
    REQUIRE(next_h2_x == Catch::Approx(h2_x));

    service.shutdown();
}

TEST_CASE("markdown layout indents lists beneath subsections beyond the heading text", "[markdown][layout]")
{
    Document document;
    document.blocks.push_back(heading(2, "Rendering"));

    Block list;
    list.kind = BlockKind::List;
    list.children.push_back(paragraph("GPU text path"));
    document.blocks.push_back(std::move(list));

    auto service = make_initialized_service();
    const auto layout = layout_markdown_document(
        document,
        default_markdown_theme(12.0f),
        service,
        LayoutOptions{ .viewport_width = 720.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() == 2);
    REQUIRE(layout.rows[0].source_kind == BlockKind::Heading);
    REQUIRE(layout.rows[1].source_kind == BlockKind::Paragraph);
    REQUIRE_FALSE(layout.rows[1].decorations.empty());

    const float heading_x = layout.rows[0].runs.front().x;
    const auto bullet = std::ranges::find_if(layout.rows[1].decorations, [](const Decoration& decoration) {
        return decoration.kind == Decoration::Kind::Bullet;
    });
    REQUIRE(bullet != layout.rows[1].decorations.end());
    REQUIRE(bullet->x > heading_x);
    REQUIRE(layout.rows[1].runs.front().x > bullet->x);

    service.shutdown();
}

TEST_CASE("markdown layout creates front matter rows with a background decoration", "[markdown][layout]")
{
    Block front_matter;
    front_matter.kind = BlockKind::FrontMatter;
    front_matter.front_matter.push_back({ "title", "Draxul Notes" });
    front_matter.front_matter.push_back({ "tags", "markdown, renderer" });

    Document document;
    document.blocks.push_back(std::move(front_matter));

    auto service = make_initialized_service();
    const auto layout = layout_markdown_document(
        document,
        default_markdown_theme(12.0f),
        service,
        LayoutOptions{ .viewport_width = 500.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() == 2);
    REQUIRE(layout.rows[0].source_kind == BlockKind::FrontMatter);
    REQUIRE_THAT(layout.rows[0].runs.front().text, Catch::Matchers::ContainsSubstring("title"));

    const auto has_background = std::ranges::any_of(
        layout.rows[0].decorations,
        [](const Decoration& decoration) { return decoration.kind == Decoration::Kind::Background; });
    REQUIRE(has_background);

    service.shutdown();
}

TEST_CASE("markdown layout renders pipe tables with aligned cells and borders", "[markdown][layout]")
{
    Block table;
    table.kind = BlockKind::Table;
    table.table_column_count = 3;
    table.children.push_back(table_row({
                                           table_cell("Left", TableCellAlignment::Left),
                                           table_cell("Center", TableCellAlignment::Center),
                                           table_cell("Right", TableCellAlignment::Right),
                                       },
        true));
    table.children.push_back(table_row({
        table_cell("alpha", TableCellAlignment::Left),
        table_cell("b", TableCellAlignment::Center),
        table_cell("9", TableCellAlignment::Right),
    }));

    Document document;
    document.blocks.push_back(std::move(table));

    auto service = make_initialized_service();
    const auto layout = layout_markdown_document(
        document,
        default_markdown_theme(12.0f),
        service,
        LayoutOptions{ .viewport_width = 480.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() == 2);
    REQUIRE(layout.rows[0].source_kind == BlockKind::TableRow);
    REQUIRE(layout.rows[0].runs.size() == 3);
    REQUIRE(layout.rows[0].runs[0].x < layout.rows[0].runs[1].x);
    REQUIRE(layout.rows[0].runs[1].x < layout.rows[0].runs[2].x);

    const auto has_table_background = std::ranges::any_of(
        layout.rows[0].decorations,
        [](const Decoration& decoration) {
            return decoration.kind == Decoration::Kind::TableCellBackground;
        });
    const auto has_table_border = std::ranges::any_of(
        layout.rows[0].decorations,
        [](const Decoration& decoration) {
            return decoration.kind == Decoration::Kind::TableBorder;
        });
    REQUIRE(has_table_background);
    REQUIRE(has_table_border);

    service.shutdown();
}

TEST_CASE("markdown layout shares pipe table width from min and preferred content widths", "[markdown][layout]")
{
    Block table;
    table.kind = BlockKind::Table;
    table.table_column_count = 3;
    table.children.push_back(table_row({
                                           table_cell("ID", TableCellAlignment::Left),
                                           table_cell("Owner", TableCellAlignment::Left),
                                           table_cell("Outcome notes", TableCellAlignment::Left),
                                       },
        true));
    table.children.push_back(table_row({
        table_cell("A-102", TableCellAlignment::Left),
        table_cell("Rendering team", TableCellAlignment::Left),
        table_cell(
            "Long prose should receive extra room but wrap instead of taking the entire table width.",
            TableCellAlignment::Left),
    }));

    Document document;
    document.blocks.push_back(std::move(table));

    auto service = make_initialized_service();
    const auto layout = layout_markdown_document(
        document,
        default_markdown_theme(12.0f),
        service,
        LayoutOptions{ .viewport_width = 520.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() > 2);
    const auto widths = table_background_widths(layout.rows.front());

    REQUIRE(widths.size() == 3);
    REQUIRE(widths[0] >= 80.0f);
    REQUIRE(widths[1] >= widths[0]);
    REQUIRE(widths[2] > widths[1]);
    REQUIRE(widths[2] < layout.content_width * 0.60f);

    service.shutdown();
}

TEST_CASE("markdown layout wraps long pipe table cells into taller table rows", "[markdown][layout]")
{
    Block table;
    table.kind = BlockKind::Table;
    table.table_column_count = 2;
    table.children.push_back(table_row({
                                           table_cell("Name", TableCellAlignment::Left),
                                           table_cell("Notes", TableCellAlignment::Left),
                                       },
        true));
    table.children.push_back(table_row({
        table_cell("alpha", TableCellAlignment::Left),
        table_cell(
            "This cell contains enough prose to wrap across multiple visual table rows.",
            TableCellAlignment::Left),
    }));

    Document document;
    document.blocks.push_back(std::move(table));

    auto service = make_initialized_service();
    const auto layout = layout_markdown_document(
        document,
        default_markdown_theme(12.0f),
        service,
        LayoutOptions{ .viewport_width = 220.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() > 2);
    REQUIRE(std::ranges::all_of(layout.rows, [](const LayoutRow& row) {
        return row.source_kind == BlockKind::TableRow && row.height > 0.0f;
    }));

    service.shutdown();
}

TEST_CASE("markdown layout starts a new row on a soft break without a blank row", "[markdown][layout]")
{
    Document document;
    document.blocks.push_back(paragraph_of({
        text_inline("First authored line"),
        break_inline(InlineKind::SoftBreak),
        text_inline("second authored line."),
    }));

    auto service = make_initialized_service();
    const auto layout = layout_markdown_document(
        document,
        default_markdown_theme(12.0f),
        service,
        LayoutOptions{ .viewport_width = 900.0f, .viewport_height = 400.0f });

    // One row per authored line -- and critically, no empty row between them.
    REQUIRE(layout.rows.size() == 2);
    REQUIRE(layout.rows[0].runs.front().text == "First authored line");
    REQUIRE(layout.rows[1].runs.front().text == "second authored line.");
    REQUIRE(layout.rows[1].y == Catch::Approx(layout.rows[0].y + layout.rows[0].height));

    service.shutdown();
}

TEST_CASE("markdown layout keeps hard breaks on their own row", "[markdown][layout]")
{
    Document document;
    document.blocks.push_back(paragraph_of({
        text_inline("Address line one"),
        break_inline(InlineKind::LineBreak),
        text_inline("Address line two"),
    }));

    auto service = make_initialized_service();
    const auto layout = layout_markdown_document(
        document,
        default_markdown_theme(12.0f),
        service,
        LayoutOptions{ .viewport_width = 900.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() == 2);
    REQUIRE(layout.rows[0].runs.front().text == "Address line one");
    REQUIRE(layout.rows[1].runs.front().text == "Address line two");

    service.shutdown();
}

TEST_CASE("markdown layout applies inline bold and italic emphasis", "[markdown][layout]")
{
    Document document;
    document.blocks.push_back(paragraph_of({
        text_inline("plain "),
        emphasis_inline(InlineKind::Strong, "bold"),
        text_inline(" and "),
        emphasis_inline(InlineKind::Emphasis, "italic"),
        text_inline(" tail"),
    }));

    auto service = make_initialized_service();
    const auto theme = default_markdown_theme(12.0f);
    const auto layout = layout_markdown_document(
        document,
        theme,
        service,
        LayoutOptions{ .viewport_width = 900.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() == 1);
    const auto& runs = layout.rows[0].runs;
    REQUIRE(runs.size() == 5);
    REQUIRE(runs[1].text == "bold");
    REQUIRE(runs[3].text == "italic");

    const auto bold = resolve_markdown_style(theme, runs[1].style);
    const auto italic = resolve_markdown_style(theme, runs[3].style);
    const auto plain = resolve_markdown_style(theme, runs[0].style);
    REQUIRE(bold.rich_text.bold);
    REQUIRE_FALSE(bold.rich_text.italic);
    REQUIRE(italic.rich_text.italic);
    REQUIRE_FALSE(italic.rich_text.bold);
    REQUIRE_FALSE(plain.rich_text.bold);
    REQUIRE_FALSE(plain.rich_text.italic);

    // Emphasis must not change the base point size or the row metrics.
    REQUIRE(bold.rich_text.point_size == Catch::Approx(plain.rich_text.point_size));
    REQUIRE(runs[0].x < runs[1].x);
    REQUIRE(runs[1].x < runs[2].x);

    service.shutdown();
}

TEST_CASE("markdown layout keeps emphasis bold inside headings", "[markdown][layout]")
{
    Block block;
    block.kind = BlockKind::Heading;
    block.heading_level = 2;
    block.inlines.push_back(text_inline("Release "));
    block.inlines.push_back(emphasis_inline(InlineKind::Emphasis, "notes"));

    Document document;
    document.blocks.push_back(std::move(block));

    auto service = make_initialized_service();
    const auto theme = default_markdown_theme(12.0f);
    const auto layout = layout_markdown_document(
        document,
        theme,
        service,
        LayoutOptions{ .viewport_width = 900.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() == 1);
    const auto& runs = layout.rows[0].runs;
    REQUIRE(runs.size() == 2);

    const auto emphasized = resolve_markdown_style(theme, runs[1].style);
    REQUIRE(emphasized.rich_text.italic);
    REQUIRE(emphasized.rich_text.bold); // headings are bold to begin with
    REQUIRE(emphasized.rich_text.point_size == Catch::Approx(theme.heading2.rich_text.point_size));

    service.shutdown();
}

TEST_CASE("markdown layout draws task markers as accent-colored glyphs", "[markdown][layout]")
{
    Document document;
    document.blocks.push_back(task_item("Ship it", true));
    document.blocks.push_back(task_item("Still open", false));

    auto service = make_initialized_service();
    const auto theme = default_markdown_theme(12.0f);
    const auto layout = layout_markdown_document(
        document,
        theme,
        service,
        LayoutOptions{ .viewport_width = 600.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() == 2);
    for (const auto& row : layout.rows)
    {
        REQUIRE(std::ranges::none_of(row.decorations, [](const Decoration& decoration) {
            return decoration.kind == Decoration::Kind::Bullet;
        }));
    }

    const auto& checked = layout.rows[0].runs.back();
    const auto& unchecked = layout.rows[1].runs.back();
    REQUIRE(checked.text == "\xE2\x9C\x93"); // U+2713 CHECK MARK
    REQUIRE(unchecked.text == "\xE2\x96\xA1"); // U+25A1 WHITE SQUARE
    REQUIRE(checked.color.has_value());
    REQUIRE(*checked.color == theme.accent);
    REQUIRE(checked.baseline == layout.rows[0].baseline);
    REQUIRE(checked.x < layout.rows[0].runs.front().x);

    // Both markers must rasterize from the primary font and stay monochrome:
    // a color-emoji glyph would ignore the accent tint applied above.
    for (const auto* marker : { &checked, &unchecked })
    {
        const auto cluster = service.resolve_cluster(marker->text, theme.body.rich_text);
        INFO("marker: " << marker->text);
        CHECK(cluster.atlas.bitmap_size.x > 0);
        CHECK(cluster.atlas.bitmap_size.y > 0);
        CHECK_FALSE(cluster.atlas.is_color);
        CHECK(cluster.advance_px > 0.0f);
    }

    service.shutdown();
}

TEST_CASE("markdown layout keeps authored line structure from source", "[markdown][layout]")
{
    // Each authored newline is one visual row, with nothing blank between them.
    const auto parsed = parse_markdown(
        "wrapped.md",
        "A paragraph that the author\n"
        "hard-wrapped in the editor with **bold words**\n"
        "spread across the lines.\n");
    REQUIRE(parsed.ok);

    auto service = make_initialized_service();
    const auto theme = default_markdown_theme(12.0f);
    const auto layout = layout_markdown_document(
        parsed.document,
        theme,
        service,
        LayoutOptions{ .viewport_width = 1200.0f, .viewport_height = 400.0f });

    REQUIRE(layout.rows.size() == 3);
    for (size_t index = 0; index < layout.rows.size(); ++index)
    {
        INFO("row " << index);
        REQUIRE_FALSE(layout.rows[index].runs.empty());
        if (index > 0)
        {
            REQUIRE(layout.rows[index].y
                == Catch::Approx(layout.rows[index - 1].y + layout.rows[index - 1].height));
        }
    }

    REQUIRE(layout.rows[0].runs.front().text == "A paragraph that the author");
    REQUIRE(layout.rows[2].runs.front().text == "spread across the lines.");

    // The emphasis markers are consumed by the parser and expressed as a bold face.
    const auto& middle = layout.rows[1].runs;
    REQUIRE(middle.size() == 2);
    REQUIRE_THAT(middle[0].text, Catch::Matchers::Equals("hard-wrapped in the editor with"));
    REQUIRE(middle[1].text == "bold words");
    REQUIRE(resolve_markdown_style(theme, middle[1].style).rich_text.bold);
    REQUIRE_FALSE(resolve_markdown_style(theme, middle[0].style).rich_text.bold);

    // Word gaps across a style change are geometric, not literal spaces in the run.
    const auto metrics = service.metrics_for(theme.body.rich_text);
    const float space_width = static_cast<float>(metrics.cell_width);
    const float first_end = middle[0].x + space_width * static_cast<float>(middle[0].text.size());
    REQUIRE(middle[1].x == Catch::Approx(first_end + space_width));

    service.shutdown();
}

TEST_CASE("markdown layout keeps bold-led lines and bullets separated", "[markdown][layout]")
{
    // The shape that regressed: consecutive lines each opening with bold, both as
    // a bare paragraph and as a real bullet list whose items wrap.
    const auto parsed = parse_markdown(
        "terms.md",
        "**Alpha** the first term\n"
        "**Beta** the second term\n"
        "**Gamma** the third term\n"
        "\n"
        "- **number** priority and sequence, unique within the pending directory\n"
        "- **slug** hyphenated short description\n");
    REQUIRE(parsed.ok);

    auto service = make_initialized_service();
    const auto theme = default_markdown_theme(12.0f);
    const auto layout = layout_markdown_document(
        parsed.document,
        theme,
        service,
        LayoutOptions{ .viewport_width = 1200.0f, .viewport_height = 600.0f });

    REQUIRE(layout.rows.size() == 5);

    const auto leading_text = [](const LayoutRow& row) {
        REQUIRE_FALSE(row.runs.empty());
        return row.runs.front().text;
    };

    REQUIRE(leading_text(layout.rows[0]) == "Alpha");
    REQUIRE(leading_text(layout.rows[1]) == "Beta");
    REQUIRE(leading_text(layout.rows[2]) == "Gamma");
    REQUIRE(leading_text(layout.rows[3]) == "number");
    REQUIRE(leading_text(layout.rows[4]) == "slug");

    for (const auto& row : layout.rows)
        REQUIRE(resolve_markdown_style(theme, row.runs.front().style).rich_text.bold);

    // The two list items keep their bullets and sit at a deeper indent.
    for (size_t index = 3; index < 5; ++index)
    {
        INFO("list row " << index);
        REQUIRE(std::ranges::any_of(layout.rows[index].decorations, [](const Decoration& decoration) {
            return decoration.kind == Decoration::Kind::Bullet;
        }));
        REQUIRE(layout.rows[index].runs.front().x > layout.rows[0].runs.front().x);
    }

    service.shutdown();
}
