#include <catch2/catch_all.hpp>

#include <draxul/text_service.h>

#include "../libs/draxul-font/src/box_drawing.h"

#include <cmath>
#include <filesystem>

using namespace draxul;

namespace
{

std::filesystem::path repo_root()
{
    auto here = std::filesystem::path(__FILE__).parent_path();
    return here.parent_path();
}

std::filesystem::path primary_font_path()
{
    return repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
}

uint8_t at(const std::vector<uint8_t>& alpha, int w, int x, int y)
{
    return alpha[(size_t)y * w + x];
}

} // namespace

TEST_CASE("synthesized_box_codepoint detects box drawing and block elements")
{
    CHECK(synthesized_box_codepoint("─") == 0x2500); // ─
    CHECK(synthesized_box_codepoint("╿") == 0x257F); // ╿
    CHECK(synthesized_box_codepoint("█") == 0x2588); // █
    CHECK(synthesized_box_codepoint("▟") == 0x259F); // ▟

    CHECK(synthesized_box_codepoint("A") == 0);
    CHECK(synthesized_box_codepoint("■") == 0); // ■ (geometric shapes, not synthesized)
    CHECK(synthesized_box_codepoint("⓿") == 0);
    CHECK(synthesized_box_codepoint("") == 0);
    CHECK(synthesized_box_codepoint("█x") == 0); // multi-codepoint cluster
}

TEST_CASE("full block covers every pixel of the cell")
{
    const int w = 18;
    const int h = 39;
    auto alpha = render_box_glyph(0x2588, w, h);
    REQUIRE(alpha.size() == (size_t)w * h);
    for (uint8_t a : alpha)
        CHECK(a == 255);
}

TEST_CASE("half blocks split the cell exactly with no seam")
{
    const int w = 18;
    const int h = 39;

    auto upper = render_box_glyph(0x2580, w, h); // ▀
    auto lower = render_box_glyph(0x2584, w, h); // ▄
    auto left = render_box_glyph(0x258C, w, h); // ▌
    auto right = render_box_glyph(0x2590, w, h); // ▐

    // Top-left pixel of ▀ and bottom-left of ▄ are solid; corners of the
    // empty halves are fully transparent.
    CHECK(at(upper, w, 0, 0) == 255);
    CHECK(at(upper, w, 0, h - 1) == 0);
    CHECK(at(lower, w, 0, h - 1) == 255);
    CHECK(at(lower, w, 0, 0) == 0);

    // ▀ stacked above ▄ in the same column tiles to a full column. At a
    // fractional midpoint both halves round 50% coverage up, so the sum may
    // be 256 — anything below 255 would be a visible seam.
    for (int y = 0; y < h; y++)
        CHECK((int)at(upper, w, 0, y) + (int)at(lower, w, 0, y) >= 255);

    // ▌ next to ▐ tiles to a full row.
    for (int x = 0; x < w; x++)
        CHECK((int)at(left, w, x, 0) + (int)at(right, w, x, 0) >= 255);
}

TEST_CASE("quadrant complements tile to a full block")
{
    const int w = 17; // odd sizes exercise the fractional midpoint
    const int h = 39;
    auto ul = render_box_glyph(0x2598, w, h); // ▘
    auto rest = render_box_glyph(0x259F, w, h); // ▟
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            CHECK((int)at(ul, w, x, y) + (int)at(rest, w, x, y) >= 255);
}

TEST_CASE("light lines span the full cell so runs connect across cells")
{
    const int w = 18;
    const int h = 39;

    auto horizontal = render_box_glyph(0x2500, w, h); // ─
    CHECK(at(horizontal, w, 0, h / 2) == 255);
    CHECK(at(horizontal, w, w - 1, h / 2) == 255);

    auto vertical = render_box_glyph(0x2502, w, h); // │
    CHECK(at(vertical, w, w / 2, 0) == 255);
    CHECK(at(vertical, w, w / 2, h - 1) == 255);

    auto double_h = render_box_glyph(0x2550, w, h); // ═
    int solid_columns = 0;
    for (int y = 0; y < h; y++)
        if (at(double_h, w, 0, y) == 255 && at(double_h, w, w - 1, y) == 255)
            solid_columns++;
    CHECK(solid_columns >= 2); // two rails reach both cell edges
}

TEST_CASE("corner joins meet at the centre")
{
    const int w = 18;
    const int h = 39;
    auto corner = render_box_glyph(0x250C, w, h); // ┌
    // Arms reach the right and bottom edges...
    CHECK(at(corner, w, w - 1, h / 2) == 255);
    CHECK(at(corner, w, w / 2, h - 1) == 255);
    // ...but not the left or top edges.
    CHECK(at(corner, w, 0, h / 2) == 0);
    CHECK(at(corner, w, w / 2, 0) == 0);
    // The junction itself is filled.
    CHECK(at(corner, w, w / 2, h / 2) == 255);
}

TEST_CASE("shades render uniform partial coverage")
{
    const int w = 18;
    const int h = 39;
    auto light = render_box_glyph(0x2591, w, h); // ░
    auto medium = render_box_glyph(0x2592, w, h); // ▒
    auto dark = render_box_glyph(0x2593, w, h); // ▓
    CHECK((int)at(light, w, 5, 5) < (int)at(medium, w, 5, 5));
    CHECK((int)at(medium, w, 5, 5) < (int)at(dark, w, 5, 5));
    CHECK(at(light, w, 5, 5) > 0);
    CHECK(at(dark, w, 5, 5) < 255);
}

TEST_CASE("TextService resolves block elements to cell-exact synthesized glyphs")
{
    TextServiceConfig config;
    config.font_path = primary_font_path().string();
    config.enable_ligatures = false;

    TextService service;
    REQUIRE(service.initialize(config, 11.0f, 192.0f));

    const FontMetrics& m = service.metrics();
    AtlasRegion region = service.resolve_cluster("█"); // █
    CHECK(region.bitmap_size.x == m.cell_width);
    CHECK(region.bitmap_size.y == m.cell_height);
    CHECK(region.bitmap_bearing.x == 0);
    CHECK(region.bitmap_bearing.y == m.ascender);
    CHECK(region.advance_px == m.cell_width);

    // The atlas pixels for █ are fully opaque, including the cell edges that
    // previously showed anti-aliased seams.
    const uint8_t* atlas = service.atlas_data();
    const int atlas_w = service.atlas_width();
    const int ax = (int)std::lround(region.uv.x * atlas_w);
    const int ay = (int)std::lround(region.uv.y * atlas_w);
    for (int row = 0; row < m.cell_height; row++)
        for (int col = 0; col < m.cell_width; col++)
            REQUIRE(atlas[(((size_t)(ay + row) * atlas_w) + ax + col) * 4 + 3] == 255);

    // Repeated resolution is served from the cluster cache.
    AtlasRegion again = service.resolve_cluster("█");
    CHECK(again.uv == region.uv);
}
