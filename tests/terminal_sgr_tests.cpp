
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <draxul/terminal_sgr.h>

using namespace draxul;

// Helper: apply a single SGR sequence and return the resulting attribute.
static HlAttr sgr(const std::vector<int>& params)
{
    HlAttr attr{};
    apply_sgr(attr, params);
    return attr;
}

// Helper: compare two Colors component-wise with a tolerance for float rounding.
static void require_color_approx(const Color& actual, float r, float g, float b, float a = 1.0f)
{
    REQUIRE_THAT(actual.r, Catch::Matchers::WithinAbs(r, 0.011));
    REQUIRE_THAT(actual.g, Catch::Matchers::WithinAbs(g, 0.011));
    REQUIRE_THAT(actual.b, Catch::Matchers::WithinAbs(b, 0.011));
    REQUIRE_THAT(actual.a, Catch::Matchers::WithinAbs(a, 0.011));
}

// ---------------------------------------------------------------------------
// ansi_color (tested indirectly through apply_sgr with fg codes 30-37)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: basic ANSI foreground colors 30-37", "[sgr]")
{
    // Index 0 (black) via SGR code 30
    auto a0 = sgr({ 30 });
    REQUIRE(a0.has_fg);
    require_color_approx(a0.fg, 0.05f, 0.06f, 0.07f);

    // Index 1 (red) via SGR code 31
    auto a1 = sgr({ 31 });
    REQUIRE(a1.has_fg);
    require_color_approx(a1.fg, 0.80f, 0.24f, 0.24f);

    // Index 2 (green)
    auto a2 = sgr({ 32 });
    require_color_approx(a2.fg, 0.40f, 0.73f, 0.42f);

    // Index 3 (yellow)
    auto a3 = sgr({ 33 });
    require_color_approx(a3.fg, 0.88f, 0.73f, 0.30f);

    // Index 4 (blue)
    auto a4 = sgr({ 34 });
    require_color_approx(a4.fg, 0.29f, 0.51f, 0.82f);

    // Index 5 (magenta)
    auto a5 = sgr({ 35 });
    require_color_approx(a5.fg, 0.70f, 0.41f, 0.78f);

    // Index 6 (cyan)
    auto a6 = sgr({ 36 });
    require_color_approx(a6.fg, 0.28f, 0.73f, 0.80f);

    // Index 7 (white)
    auto a7 = sgr({ 37 });
    require_color_approx(a7.fg, 0.84f, 0.84f, 0.85f);
}

// ---------------------------------------------------------------------------
// xterm_color: indices 0-7 (same as ANSI), tested via 256-color fg mode
// ---------------------------------------------------------------------------

TEST_CASE("SGR: xterm 256-color indices 0-7 match basic ANSI palette", "[sgr]")
{
    // 38;5;0 should match SGR 30
    auto via_256 = sgr({ 38, 5, 0 });
    auto via_basic = sgr({ 30 });
    REQUIRE(via_256.fg == via_basic.fg);

    // 38;5;7 should match SGR 37
    via_256 = sgr({ 38, 5, 7 });
    via_basic = sgr({ 37 });
    REQUIRE(via_256.fg == via_basic.fg);
}

// ---------------------------------------------------------------------------
// xterm_color: indices 8-15 (bright), tested via 256-color fg mode
// ---------------------------------------------------------------------------

TEST_CASE("SGR: xterm 256-color indices 8-15 match bright palette", "[sgr]")
{
    // 38;5;8 should match SGR 90 (bright black)
    auto via_256 = sgr({ 38, 5, 8 });
    auto via_bright = sgr({ 90 });
    REQUIRE(via_256.fg == via_bright.fg);

    // 38;5;15 should match SGR 97 (bright white)
    via_256 = sgr({ 38, 5, 15 });
    via_bright = sgr({ 97 });
    REQUIRE(via_256.fg == via_bright.fg);

    // 38;5;9 = bright red
    auto a = sgr({ 38, 5, 9 });
    require_color_approx(a.fg, 0.94f, 0.38f, 0.38f);
}

// ---------------------------------------------------------------------------
// xterm_color: indices 16-231 (6x6x6 color cube)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: xterm 256-color cube (indices 16-231)", "[sgr]")
{
    // Index 16 = r=0, g=0, b=0 -> all channels 0/255
    auto a16 = sgr({ 38, 5, 16 });
    require_color_approx(a16.fg, 0.0f, 0.0f, 0.0f);

    // Index 196 = (196-16)=180 -> r=180/36=5, g=(180/6)%6=0, b=180%6=0
    // r=5 -> 255/255=1.0, g=0 -> 0/255=0.0, b=0 -> 0.0
    auto a196 = sgr({ 38, 5, 196 });
    require_color_approx(a196.fg, 1.0f, 0.0f, 0.0f);

    // Index 21 = (21-16)=5 -> r=0, g=0, b=5 -> pure blue (255/255)
    auto a21 = sgr({ 38, 5, 21 });
    require_color_approx(a21.fg, 0.0f, 0.0f, 1.0f);

    // Index 46 = (46-16)=30 -> r=0, g=30/6%6=5, b=0 -> pure green (255/255)
    auto a46 = sgr({ 38, 5, 46 });
    require_color_approx(a46.fg, 0.0f, 1.0f, 0.0f);

    // Index 231 = (231-16)=215 -> r=5, g=5, b=5 -> white (255/255)
    auto a231 = sgr({ 38, 5, 231 });
    require_color_approx(a231.fg, 1.0f, 1.0f, 1.0f);

    // Index 17 = (17-16)=1 -> r=0, g=0, b=1 -> 95/255 ~ 0.373
    auto a17 = sgr({ 38, 5, 17 });
    require_color_approx(a17.fg, 0.0f, 0.0f, 95.0f / 255.0f);
}

// ---------------------------------------------------------------------------
// xterm_color: indices 232-255 (grayscale ramp)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: xterm 256-color grayscale ramp (indices 232-255)", "[sgr]")
{
    // Index 232: gray = (8 + 0*10) / 255 = 8/255
    auto a232 = sgr({ 38, 5, 232 });
    float expected = 8.0f / 255.0f;
    require_color_approx(a232.fg, expected, expected, expected);

    // Index 255: gray = (8 + 23*10) / 255 = 238/255
    auto a255 = sgr({ 38, 5, 255 });
    expected = 238.0f / 255.0f;
    require_color_approx(a255.fg, expected, expected, expected);

    // Index 244 (midpoint-ish): gray = (8 + 12*10) / 255 = 128/255
    auto a244 = sgr({ 38, 5, 244 });
    expected = 128.0f / 255.0f;
    require_color_approx(a244.fg, expected, expected, expected);
}

// ---------------------------------------------------------------------------
// xterm_color: out of range (clamped to 0-255)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: xterm 256-color out-of-range indices are clamped", "[sgr]")
{
    // Negative index should clamp to 0, same as ANSI black
    auto neg = sgr({ 38, 5, -1 });
    auto zero = sgr({ 38, 5, 0 });
    REQUIRE(neg.fg == zero.fg);

    // Index 256+ should clamp to 255 (last grayscale entry)
    auto over = sgr({ 38, 5, 999 });
    auto max_idx = sgr({ 38, 5, 255 });
    REQUIRE(over.fg == max_idx.fg);
}

// ---------------------------------------------------------------------------
// apply_sgr: reset (code 0)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: code 0 resets all attributes", "[sgr]")
{
    HlAttr attr{};
    apply_sgr(attr, { 1, 3, 4, 9, 31, 42 });
    REQUIRE(attr.bold);
    REQUIRE(attr.italic);
    REQUIRE(attr.underline);
    REQUIRE(attr.strikethrough);
    REQUIRE(attr.has_fg);
    REQUIRE(attr.has_bg);

    apply_sgr(attr, { 0 });
    HlAttr fresh{};
    REQUIRE(attr == fresh);
}

// ---------------------------------------------------------------------------
// apply_sgr: text styling attributes
// ---------------------------------------------------------------------------

TEST_CASE("SGR: bold (1)", "[sgr]")
{
    auto a = sgr({ 1 });
    REQUIRE(a.bold);
    REQUIRE_FALSE(a.italic);
}

TEST_CASE("SGR: italic (3)", "[sgr]")
{
    auto a = sgr({ 3 });
    REQUIRE(a.italic);
}

TEST_CASE("SGR: underline (4)", "[sgr]")
{
    auto a = sgr({ 4 });
    REQUIRE(a.underline);
}

TEST_CASE("SGR: reverse (7)", "[sgr]")
{
    auto a = sgr({ 7 });
    REQUIRE(a.reverse);
}

TEST_CASE("SGR: strikethrough (9)", "[sgr]")
{
    auto a = sgr({ 9 });
    REQUIRE(a.strikethrough);
}

// ---------------------------------------------------------------------------
// apply_sgr: attribute disable codes
// ---------------------------------------------------------------------------

TEST_CASE("SGR: disable bold (22), italic (23), underline (24), reverse (27), strikethrough (29)", "[sgr]")
{
    HlAttr attr{};
    apply_sgr(attr, { 1, 3, 4, 7, 9 });
    REQUIRE(attr.bold);
    REQUIRE(attr.italic);
    REQUIRE(attr.underline);
    REQUIRE(attr.reverse);
    REQUIRE(attr.strikethrough);

    apply_sgr(attr, { 22 });
    REQUIRE_FALSE(attr.bold);
    REQUIRE(attr.italic); // others unchanged

    apply_sgr(attr, { 23 });
    REQUIRE_FALSE(attr.italic);

    apply_sgr(attr, { 24 });
    REQUIRE_FALSE(attr.underline);

    apply_sgr(attr, { 27 });
    REQUIRE_FALSE(attr.reverse);

    apply_sgr(attr, { 29 });
    REQUIRE_FALSE(attr.strikethrough);
}

// ---------------------------------------------------------------------------
// apply_sgr: foreground basic colors (30-37)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: foreground basic colors set has_fg", "[sgr]")
{
    for (int code = 30; code <= 37; ++code)
    {
        auto a = sgr({ code });
        REQUIRE(a.has_fg);
        REQUIRE_FALSE(a.has_bg);
    }
}

// ---------------------------------------------------------------------------
// apply_sgr: bright foreground (90-97)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: bright foreground colors 90-97", "[sgr]")
{
    // SGR 90 = bright black (ANSI index 8)
    auto a90 = sgr({ 90 });
    REQUIRE(a90.has_fg);
    require_color_approx(a90.fg, 0.33f, 0.34f, 0.35f);

    // SGR 97 = bright white (ANSI index 15)
    auto a97 = sgr({ 97 });
    require_color_approx(a97.fg, 0.97f, 0.98f, 0.98f);
}

// ---------------------------------------------------------------------------
// apply_sgr: background basic colors (40-47)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: background basic colors 40-47", "[sgr]")
{
    // SGR 40 = bg black (ANSI index 0)
    auto a40 = sgr({ 40 });
    REQUIRE(a40.has_bg);
    REQUIRE_FALSE(a40.has_fg);
    require_color_approx(a40.bg, 0.05f, 0.06f, 0.07f);

    // SGR 47 = bg white (ANSI index 7)
    auto a47 = sgr({ 47 });
    require_color_approx(a47.bg, 0.84f, 0.84f, 0.85f);
}

// ---------------------------------------------------------------------------
// apply_sgr: bright background (100-107)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: bright background colors 100-107", "[sgr]")
{
    // SGR 100 = bright bg black (ANSI index 8)
    auto a100 = sgr({ 100 });
    REQUIRE(a100.has_bg);
    require_color_approx(a100.bg, 0.33f, 0.34f, 0.35f);

    // SGR 107 = bright bg white (ANSI index 15)
    auto a107 = sgr({ 107 });
    require_color_approx(a107.bg, 0.97f, 0.98f, 0.98f);
}

// ---------------------------------------------------------------------------
// apply_sgr: 256-color mode (38;5;N for fg, 48;5;N for bg)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: 256-color foreground via 38;5;N", "[sgr]")
{
    auto a = sgr({ 38, 5, 196 }); // bright red from cube
    REQUIRE(a.has_fg);
    require_color_approx(a.fg, 1.0f, 0.0f, 0.0f);
}

TEST_CASE("SGR: 256-color background via 48;5;N", "[sgr]")
{
    auto a = sgr({ 48, 5, 21 }); // pure blue from cube
    REQUIRE(a.has_bg);
    require_color_approx(a.bg, 0.0f, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// apply_sgr: 24-bit RGB mode (38;2;R;G;B for fg, 48;2;R;G;B for bg)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: 24-bit RGB foreground via 38;2;R;G;B", "[sgr]")
{
    auto a = sgr({ 38, 2, 128, 64, 255 });
    REQUIRE(a.has_fg);
    require_color_approx(a.fg, 128.0f / 255.0f, 64.0f / 255.0f, 1.0f);
}

TEST_CASE("SGR: 24-bit RGB background via 48;2;R;G;B", "[sgr]")
{
    auto a = sgr({ 48, 2, 0, 255, 128 });
    REQUIRE(a.has_bg);
    require_color_approx(a.bg, 0.0f, 1.0f, 128.0f / 255.0f);
}

TEST_CASE("SGR: 24-bit RGB clamps components to 0-255", "[sgr]")
{
    auto a = sgr({ 38, 2, 300, -10, 128 });
    REQUIRE(a.has_fg);
    require_color_approx(a.fg, 1.0f, 0.0f, 128.0f / 255.0f);
}

// ---------------------------------------------------------------------------
// apply_sgr: default fg (39), default bg (49)
// ---------------------------------------------------------------------------

TEST_CASE("SGR: code 39 clears foreground, code 49 clears background", "[sgr]")
{
    HlAttr attr{};
    apply_sgr(attr, { 31, 42 }); // set red fg, green bg
    REQUIRE(attr.has_fg);
    REQUIRE(attr.has_bg);

    apply_sgr(attr, { 39 });
    REQUIRE_FALSE(attr.has_fg);
    REQUIRE(attr.has_bg); // bg still set

    apply_sgr(attr, { 49 });
    REQUIRE_FALSE(attr.has_bg);
}

// ---------------------------------------------------------------------------
// apply_sgr: multiple codes in one sequence
// ---------------------------------------------------------------------------

TEST_CASE("SGR: multiple codes applied in a single call", "[sgr]")
{
    auto a = sgr({ 1, 3, 4, 38, 2, 255, 128, 0, 48, 5, 21 });
    REQUIRE(a.bold);
    REQUIRE(a.italic);
    REQUIRE(a.underline);
    REQUIRE(a.has_fg);
    require_color_approx(a.fg, 1.0f, 128.0f / 255.0f, 0.0f);
    REQUIRE(a.has_bg);
    require_color_approx(a.bg, 0.0f, 0.0f, 1.0f); // xterm index 21 = pure blue
}

// ---------------------------------------------------------------------------
// apply_sgr: empty parameter list acts as reset
// ---------------------------------------------------------------------------

TEST_CASE("SGR: empty parameter list is treated as reset", "[sgr]")
{
    HlAttr attr{};
    apply_sgr(attr, { 1, 31 });
    REQUIRE(attr.bold);
    REQUIRE(attr.has_fg);

    apply_sgr(attr, {});
    HlAttr fresh{};
    REQUIRE(attr == fresh);
}

// ---------------------------------------------------------------------------
// apply_sgr: extended color with insufficient parameters is a no-op
// ---------------------------------------------------------------------------

TEST_CASE("SGR: 38 with no sub-parameters is a no-op", "[sgr]")
{
    auto a = sgr({ 38 });
    REQUIRE_FALSE(a.has_fg);
}

TEST_CASE("SGR: 38;5 with no color index is a no-op", "[sgr]")
{
    auto a = sgr({ 38, 5 });
    REQUIRE_FALSE(a.has_fg);
}

TEST_CASE("SGR: 38;2 with insufficient RGB components is a no-op", "[sgr]")
{
    auto a = sgr({ 38, 2, 128, 64 }); // missing B
    REQUIRE_FALSE(a.has_fg);
}

// ---------------------------------------------------------------------------
// apply_sgr: cumulative application across multiple calls
// ---------------------------------------------------------------------------

TEST_CASE("SGR: attributes accumulate across multiple apply_sgr calls", "[sgr]")
{
    HlAttr attr{};
    apply_sgr(attr, { 1 }); // bold
    apply_sgr(attr, { 3 }); // italic
    apply_sgr(attr, { 31 }); // red fg

    REQUIRE(attr.bold);
    REQUIRE(attr.italic);
    REQUIRE(attr.has_fg);
    require_color_approx(attr.fg, 0.80f, 0.24f, 0.24f);
}
