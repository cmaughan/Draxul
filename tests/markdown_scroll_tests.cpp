#include <catch2/catch_all.hpp>

#include <draxul/markdown/markdown_layout.h>
#include <draxul/markdown/markdown_scroll.h>

using namespace draxul::markdown;

TEST_CASE("markdown scroll state clamps offset to the valid range", "[markdown][scroll]")
{
    MarkdownScrollState scroll;
    scroll.set_viewport_height(100.0f);
    scroll.set_content_height(350.0f);

    scroll.scroll_pixels(75.0f);
    REQUIRE(scroll.offset() == 75.0f);

    scroll.scroll_pixels(1000.0f);
    REQUIRE(scroll.offset() == scroll.max_offset());
    REQUIRE(scroll.max_offset() == 250.0f);

    scroll.scroll_pixels(-1000.0f);
    REQUIRE(scroll.offset() == 0.0f);

    scroll.end();
    REQUIRE(scroll.offset() == 250.0f);

    scroll.home();
    REQUIRE(scroll.offset() == 0.0f);
}

TEST_CASE("markdown scroll state reports visible scrollbar and proportional thumb", "[markdown][scroll]")
{
    MarkdownScrollState scroll;
    scroll.set_viewport_height(100.0f);
    scroll.set_content_height(400.0f);
    scroll.scroll_pixels(150.0f);

    REQUIRE(scroll.scrollbar_visible());

    const auto track = scroll.scrollbar_track(10.0f, 20.0f, 8.0f, 100.0f);
    const auto thumb = scroll.scrollbar_thumb(10.0f, 20.0f, 8.0f, 100.0f);

    REQUIRE(track.x == 10.0f);
    REQUIRE(track.y == 20.0f);
    REQUIRE(track.width == 8.0f);
    REQUIRE(track.height == 100.0f);

    REQUIRE(thumb.height == 25.0f);
    REQUIRE(thumb.y == 57.5f);
    REQUIRE(thumb.width == 8.0f);
}

TEST_CASE("markdown scroll state supports line-sized navigation steps", "[markdown][scroll]")
{
    MarkdownScrollState scroll;
    scroll.set_viewport_height(100.0f);
    scroll.set_content_height(400.0f);

    scroll.line_down(18.0f);
    REQUIRE(scroll.offset() == 18.0f);

    scroll.line_up(8.0f);
    REQUIRE(scroll.offset() == 10.0f);

    scroll.line_up(100.0f);
    REQUIRE(scroll.offset() == 0.0f);
}

TEST_CASE("markdown scroll state maps scrollbar thumb drag to document offset", "[markdown][scroll]")
{
    MarkdownScrollState scroll;
    scroll.set_viewport_height(100.0f);
    scroll.set_content_height(400.0f);

    const auto thumb = scroll.scrollbar_thumb(10.0f, 20.0f, 8.0f, 100.0f);
    REQUIRE(thumb.height == 25.0f);

    scroll.drag_thumb_to(20.0f, 100.0f, 70.0f, 12.5f);
    REQUIRE(scroll.offset() == 150.0f);

    scroll.drag_thumb_to(20.0f, 100.0f, 200.0f, 12.5f);
    REQUIRE(scroll.offset() == scroll.max_offset());
}

TEST_CASE("visible rows returns first row and count for variable heights", "[markdown][scroll]")
{
    LayoutDocument document;
    document.rows.push_back(LayoutRow{ .y = 0.0f, .height = 20.0f });
    document.rows.push_back(LayoutRow{ .y = 20.0f, .height = 40.0f });
    document.rows.push_back(LayoutRow{ .y = 60.0f, .height = 10.0f });
    document.rows.push_back(LayoutRow{ .y = 70.0f, .height = 30.0f });

    auto range = visible_rows(document, 0.0f, 50.0f);
    REQUIRE(range.first == 0);
    REQUIRE(range.count == 2);

    range = visible_rows(document, 25.0f, 45.0f);
    REQUIRE(range.first == 1);
    REQUIRE(range.count == 2);

    range = visible_rows(document, 70.0f, 10.0f);
    REQUIRE(range.first == 3);
    REQUIRE(range.count == 1);
}
