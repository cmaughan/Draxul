#include <catch2/catch_all.hpp>

#include "support/temp_dir.h"
#include "support/test_host_callbacks.h"

#include <draxul/app_config.h>
#include <draxul/host_kind.h>
#include <draxul/markdown/markdown_host.h>
#include <draxul/markdown/markdown_parser.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>

using namespace draxul;
using namespace draxul::tests;
using namespace draxul::markdown;

namespace draxul::markdown
{

struct MarkdownHostMetricSnapshot
{
    FontMetrics body_metrics{};
    float row_y = 0.0f;
    float row_height = 0.0f;
    float row_baseline = 0.0f;
    float expected_height = 0.0f;
    float expected_baseline = 0.0f;
    float max_glyph_height = 0.0f;
    size_t glyph_count = 0;
    bool found_body_row = false;
    bool glyphs_overlap_row = false;
};

class MarkdownHostTestAccess
{
public:
    static MarkdownHostMetricSnapshot metric_snapshot(MarkdownHost& host)
    {
        host.pump();
        MarkdownHostMetricSnapshot snapshot;
        snapshot.body_metrics = host.rich_text_.metrics_for(
            host.theme_.body.rich_text);

        const auto body_row = std::ranges::find_if(
            host.layout_.rows,
            [](const LayoutRow& row) {
                return row.source_kind == BlockKind::Paragraph;
            });
        if (body_row == host.layout_.rows.end())
            return snapshot;

        snapshot.found_body_row = true;
        snapshot.row_y = body_row->y;
        snapshot.row_height = body_row->height;
        snapshot.row_baseline = body_row->baseline;
        snapshot.expected_height = std::max(1.0f,
            static_cast<float>(snapshot.body_metrics.cell_height)
                * host.theme_.body.line_height_multiplier);
        const float leading = std::max(0.0f,
            snapshot.expected_height
                - static_cast<float>(snapshot.body_metrics.cell_height));
        snapshot.expected_baseline = body_row->y + leading * 0.5f
            + static_cast<float>(snapshot.body_metrics.ascender);

        LayoutDocument body_document;
        body_document.content_width = host.layout_.content_width;
        body_document.content_height = body_row->y + body_row->height;
        body_document.rows.push_back(*body_row);
        const auto draw_list = build_markdown_draw_list(
            body_document,
            host.theme_,
            host.rich_text_,
            MarkdownDrawListOptions{
                .viewport_width = std::max(1, host.viewport_.pixel_size.x),
                .viewport_height = std::max(1, host.viewport_.pixel_size.y),
                .pixel_scale = host.viewport_.pixel_scale,
            });
        snapshot.glyph_count = draw_list.glyphs.size();
        for (const auto& glyph : draw_list.glyphs)
        {
            snapshot.max_glyph_height = std::max(
                snapshot.max_glyph_height, glyph.rect.w);
            snapshot.glyphs_overlap_row = snapshot.glyphs_overlap_row
                || (glyph.rect.y < body_row->y + body_row->height
                    && glyph.rect.y + glyph.rect.w > body_row->y);
        }
        return snapshot;
    }
};

} // namespace draxul::markdown

namespace
{
bool has_inline_kind(const Block& block, InlineKind kind)
{
    return std::ranges::any_of(block.inlines, [kind](const Inline& inline_node) {
        return inline_node.kind == kind;
    });
}
} // namespace

TEST_CASE("markdown parser builds heading and paragraph blocks", "[markdown][parser]")
{
    const auto result = parse_markdown("test.md", "# Title\n\nHello **world**.\n");

    REQUIRE(result.ok);
    REQUIRE(result.document.source_path == "test.md");
    REQUIRE(result.document.source_text == "# Title\n\nHello **world**.\n");
    REQUIRE(result.document.blocks.size() == 2);
    REQUIRE(result.document.blocks[0].kind == BlockKind::Heading);
    REQUIRE(result.document.blocks[0].heading_level == 1);
    REQUIRE(result.document.blocks[1].kind == BlockKind::Paragraph);
    REQUIRE(result.document.blocks[0].source.byte_length > 0);
    REQUIRE(result.document.blocks[1].source.byte_length > 0);
}

TEST_CASE("markdown parser recognizes fenced code block language and literal",
    "[markdown][parser]")
{
    const auto result = parse_markdown("test.md", "```cpp\nint answer = 42;\n```\n");

    REQUIRE(result.ok);
    REQUIRE(result.document.blocks.size() == 1);
    const auto& code = result.document.blocks[0];
    REQUIRE(code.kind == BlockKind::CodeBlock);
    REQUIRE(code.language == "cpp");
    REQUIRE(code.literal == "int answer = 42;\n");
    REQUIRE(code.source.byte_length > 0);
}

TEST_CASE("markdown parser recognizes unordered task list items", "[markdown][parser]")
{
    const auto result = parse_markdown("tasks.md", "- [x] Done\n- [ ] Later\n");

    REQUIRE(result.ok);
    REQUIRE(result.document.blocks.size() == 1);
    const auto& list = result.document.blocks[0];
    REQUIRE(list.kind == BlockKind::List);
    REQUIRE_FALSE(list.ordered);
    REQUIRE(list.children.size() == 2);
    REQUIRE(list.children[0].kind == BlockKind::TaskItem);
    REQUIRE(list.children[0].checked);
    REQUIRE(list.children[1].kind == BlockKind::TaskItem);
    REQUIRE_FALSE(list.children[1].checked);
}

TEST_CASE("markdown parser recognizes inline strong emphasis code and link",
    "[markdown][parser]")
{
    const auto result = parse_markdown(
        "inline.md",
        "This has **strong**, *emphasis*, `code`, and [link text](https://example.com).\n");

    REQUIRE(result.ok);
    REQUIRE(result.document.blocks.size() == 1);
    const auto& paragraph = result.document.blocks[0];
    REQUIRE(paragraph.kind == BlockKind::Paragraph);
    REQUIRE(has_inline_kind(paragraph, InlineKind::Strong));
    REQUIRE(has_inline_kind(paragraph, InlineKind::Emphasis));
    REQUIRE(has_inline_kind(paragraph, InlineKind::Code));

    const auto link = std::ranges::find_if(paragraph.inlines, [](const Inline& inline_node) {
        return inline_node.kind == InlineKind::Link;
    });
    REQUIRE(link != paragraph.inlines.end());
    REQUIRE(link->destination == "https://example.com");
    REQUIRE(link->children.size() == 1);
    REQUIRE(link->children[0].text == "link text");
}

TEST_CASE("markdown parser recognizes thematic breaks", "[markdown][parser]")
{
    const auto result = parse_markdown("break.md", "Before\n\n---\n\nAfter\n");

    REQUIRE(result.ok);
    REQUIRE(result.document.blocks.size() == 3);
    REQUIRE(result.document.blocks[0].kind == BlockKind::Paragraph);
    REQUIRE(result.document.blocks[1].kind == BlockKind::ThematicBreak);
    REQUIRE(result.document.blocks[2].kind == BlockKind::Paragraph);
}

TEST_CASE("markdown parser recognizes pipe table rows cells and alignment",
    "[markdown][parser]")
{
    const auto result = parse_markdown(
        "table.md",
        "| Left | Center | Right |\n"
        "| :--- | :----: | ----: |\n"
        "| a | b | c |\n");

    REQUIRE(result.ok);
    REQUIRE(result.document.blocks.size() == 1);
    const auto& table = result.document.blocks[0];
    REQUIRE(table.kind == BlockKind::Table);
    REQUIRE(table.table_column_count == 3);
    REQUIRE(table.children.size() == 2);

    const auto& header = table.children[0];
    REQUIRE(header.kind == BlockKind::TableRow);
    REQUIRE(header.table_header);
    REQUIRE(header.children.size() == 3);
    REQUIRE(header.children[0].kind == BlockKind::TableCell);
    REQUIRE(header.children[0].table_header);
    REQUIRE(header.children[0].table_alignment == TableCellAlignment::Left);
    REQUIRE(header.children[1].table_alignment == TableCellAlignment::Center);
    REQUIRE(header.children[2].table_alignment == TableCellAlignment::Right);

    const auto& body = table.children[1];
    REQUIRE(body.kind == BlockKind::TableRow);
    REQUIRE_FALSE(body.table_header);
    REQUIRE(body.children.size() == 3);
    REQUIRE(body.children[0].table_alignment == TableCellAlignment::Left);
    REQUIRE(body.children[1].table_alignment == TableCellAlignment::Center);
    REQUIRE(body.children[2].table_alignment == TableCellAlignment::Right);
}

TEST_CASE("markdown host opens another source from dispatch action", "[markdown][host]")
{
    const std::string font = draxul::tests::bundled_font_path().string();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp("draxul-markdown-open-file");
    const auto first = temp.path / "first.md";
    const auto second = temp.path / "second.md";
    {
        std::ofstream out(first, std::ios::trunc);
        out << "# First\n";
    }
    {
        std::ofstream out(second, std::ios::trunc);
        out << "# Second\n";
    }

    AppConfig config;
    config.font_path = font;

    MarkdownHost host;
    CHECK(host.is_markdown_host());

    HostContext ctx;
    ctx.config = &config;
    ctx.launch_options.kind = HostKind::Markdown;
    ctx.launch_options.source_path = first.string();
    ctx.initial_viewport.pixel_size = { 800, 600 };
    ctx.display_ppi = 96.0f;
    TestHostCallbacks callbacks;
    REQUIRE(host.initialize(ctx, callbacks));
    REQUIRE(host.status_text() == "markdown | first.md");
    REQUIRE(callbacks.last_window_title == "first.md");

    REQUIRE(host.dispatch_action("open_file:" + second.string()));
    CHECK(host.status_text() == "markdown | second.md");
    CHECK(callbacks.last_window_title == "second.md");
}

TEST_CASE("markdown host keeps layout and glyph metrics aligned across font and DPI changes",
    "[markdown][host][dpi]")
{
    const std::string font = draxul::tests::bundled_font_path().string();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp("draxul-markdown-metrics");
    const auto source = temp.path / "metrics.md";
    {
        std::ofstream out(source, std::ios::trunc);
        out << "# Metric heading\n\nBody text with **emphasis**.\n";
    }

    const auto initialize_host = [&](MarkdownHost& host,
                                     AppConfig& config,
                                     TestHostCallbacks& callbacks,
                                     float display_ppi,
                                     float pixel_scale) {
        config.font_path = font;
        config.markdown.font_size = 12.0f;
        HostContext context;
        context.config = &config;
        context.launch_options.kind = HostKind::Markdown;
        context.launch_options.source_path = source.string();
        context.initial_viewport.pixel_size = { 800, 600 };
        context.initial_viewport.pixel_scale = pixel_scale;
        context.display_ppi = display_ppi;
        return host.initialize(context, callbacks);
    };
    const auto check_alignment = [](const MarkdownHostMetricSnapshot& snapshot) {
        REQUIRE(snapshot.found_body_row);
        REQUIRE(snapshot.glyph_count > 0);
        CHECK(snapshot.row_height == Catch::Approx(snapshot.expected_height));
        CHECK(snapshot.row_baseline == Catch::Approx(snapshot.expected_baseline));
        CHECK(snapshot.glyphs_overlap_row);
    };

    AppConfig standard_config;
    TestHostCallbacks standard_callbacks;
    MarkdownHost standard_host;
    REQUIRE(initialize_host(
        standard_host, standard_config, standard_callbacks, 96.0f, 1.0f));
    const auto standard = MarkdownHostTestAccess::metric_snapshot(standard_host);
    check_alignment(standard);

    HostReloadConfig larger_font;
    larger_font.markdown_font_size = 18.0f;
    larger_font.markdown_margin_columns = standard_config.markdown.margin_columns;
    standard_host.on_config_reloaded(larger_font);
    const auto enlarged = MarkdownHostTestAccess::metric_snapshot(standard_host);
    check_alignment(enlarged);
    CHECK(enlarged.body_metrics.cell_height > standard.body_metrics.cell_height);
    CHECK(enlarged.row_height > standard.row_height);
    CHECK(enlarged.max_glyph_height > standard.max_glyph_height);

    AppConfig retina_config;
    TestHostCallbacks retina_callbacks;
    MarkdownHost retina_host;
    REQUIRE(initialize_host(
        retina_host, retina_config, retina_callbacks, 192.0f, 2.0f));
    const auto retina = MarkdownHostTestAccess::metric_snapshot(retina_host);
    check_alignment(retina);
    CHECK(retina.body_metrics.cell_width > standard.body_metrics.cell_width);
    CHECK(retina.body_metrics.cell_height > standard.body_metrics.cell_height);
    CHECK(retina.row_height > standard.row_height);
    CHECK(retina.max_glyph_height > standard.max_glyph_height);
}
