#include <catch2/catch_all.hpp>

#include "support/temp_dir.h"
#include "support/test_host_callbacks.h"

#include <draxul/app_config.h>
#include <draxul/host_kind.h>
#include <draxul/markdown/markdown_host.h>
#include <draxul/markdown/markdown_parser.h>

#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>

using namespace draxul;
using namespace draxul::tests;
using namespace draxul::markdown;

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
    const std::string font = std::string(DRAXUL_PROJECT_ROOT) + "/fonts/JetBrainsMonoNerdFont-Regular.ttf";
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
