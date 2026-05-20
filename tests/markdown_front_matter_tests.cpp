#include <catch2/catch_all.hpp>

#include <draxul/markdown/markdown_parser.h>

#include <ranges>

using namespace draxul::markdown;

TEST_CASE("markdown parser extracts yaml front matter", "[markdown][frontmatter]")
{
    const auto result = parse_markdown(
        "note.md",
        "---\ntitle: Test\ntags: [one, two]\n---\n\n# Body\n");

    REQUIRE(result.ok);
    REQUIRE(result.document.blocks.size() == 2);
    REQUIRE(result.document.blocks.front().kind == BlockKind::FrontMatter);
    REQUIRE(result.document.blocks.front().front_matter.size() == 2);
    REQUIRE(result.document.blocks.front().front_matter[0].key == "title");
    REQUIRE(result.document.blocks.front().front_matter[0].value == "Test");
    REQUIRE(result.document.blocks.front().front_matter[1].key == "tags");
    REQUIRE(result.document.blocks.front().front_matter[1].value == "[one, two]");
    REQUIRE(result.document.blocks[1].kind == BlockKind::Heading);
}

TEST_CASE("markdown parser recognizes callout blockquotes", "[markdown][callout]")
{
    const auto result = parse_markdown("note.md", "> [!warning] Careful\n> Body text\n");

    REQUIRE(result.ok);
    REQUIRE(result.document.blocks.size() == 1);
    const auto& callout = result.document.blocks[0];
    REQUIRE(callout.kind == BlockKind::Callout);
    REQUIRE(callout.callout_type == "warning");
    REQUIRE(callout.callout_title == "Careful");
    REQUIRE_FALSE(callout.children.empty());
    REQUIRE(callout.children[0].kind == BlockKind::Paragraph);
}

TEST_CASE("markdown parser recognizes wikilinks", "[markdown][wikilink]")
{
    const auto result = parse_markdown("note.md", "See [[Daily Note|today]].\n");

    REQUIRE(result.ok);
    REQUIRE(result.document.blocks.size() == 1);
    const auto& paragraph = result.document.blocks[0];
    REQUIRE(paragraph.kind == BlockKind::Paragraph);
    const auto link = std::ranges::find_if(paragraph.inlines, [](const Inline& inline_node) {
        return inline_node.kind == InlineKind::Link && inline_node.destination == "Daily Note";
    });
    REQUIRE(link != paragraph.inlines.end());
    REQUIRE(link->children.size() == 1);
    REQUIRE(link->children[0].text == "today");
}
