#include <catch2/catch_all.hpp>

#include <draxul/markdown/markdown_document.h>
#include <draxul/markdown/markdown_layout.h>
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

std::filesystem::path repo_root()
{
    auto here = std::filesystem::path(__FILE__).parent_path();
    return here.parent_path();
}

TextServiceConfig rich_text_test_config()
{
    TextServiceConfig config;
    config.font_path = (repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf").string();
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
    }, true));
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
    }, true));
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
    }, true));
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
