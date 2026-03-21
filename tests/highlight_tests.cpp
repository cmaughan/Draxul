
#include <catch2/catch_test_macros.hpp>

#include <draxul/highlight.h>

using namespace draxul;

TEST_CASE("highlight table resolves explicit colors and special color", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(Color::from_rgb(0xFFFFFF));
    table.set_default_bg(Color::from_rgb(0x101010));
    table.set_default_sp(Color::from_rgb(0x00FF00));

    HlAttr attr;
    attr.fg = Color::from_rgb(0x123456);
    attr.bg = Color::from_rgb(0x654321);
    attr.sp = Color::from_rgb(0xABCDEF);
    attr.has_fg = true;
    attr.has_bg = true;
    attr.has_sp = true;

    Color fg;
    Color bg;
    Color sp;
    table.resolve(attr, fg, bg, &sp);

    REQUIRE(fg == Color::from_rgb(0x123456));
    REQUIRE(bg == Color::from_rgb(0x654321));
    REQUIRE(sp == Color::from_rgb(0xABCDEF));
}

TEST_CASE("highlight table reverses colors without losing accent color", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(Color::from_rgb(0xFFFFFF));
    table.set_default_bg(Color::from_rgb(0x101010));

    HlAttr attr;
    attr.fg = Color::from_rgb(0x123456);
    attr.bg = Color::from_rgb(0x654321);
    attr.has_fg = true;
    attr.has_bg = true;
    attr.reverse = true;

    Color fg;
    Color bg;
    Color sp;
    table.resolve(attr, fg, bg, &sp);

    REQUIRE(fg == Color::from_rgb(0x654321));
    REQUIRE(bg == Color::from_rgb(0x123456));
    REQUIRE(sp == fg);
}
