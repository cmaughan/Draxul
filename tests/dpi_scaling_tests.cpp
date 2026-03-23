
#include <catch2/catch_all.hpp>

#include <draxul/text_service.h>
#include <draxul/ui_panel.h>
#include <filesystem>

using namespace draxul;

namespace
{

std::filesystem::path bundled_font_path()
{
    auto here = std::filesystem::path(__FILE__).parent_path();
    return here.parent_path() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
}

// Mirrors SdlWindow::display_ppi(): ppi = 96 * display_scale
float compute_display_ppi(float display_scale)
{
    return 96.0f * display_scale;
}

} // namespace

// --- Display PPI formula ---

TEST_CASE("dpi 1.0x scale produces 96 ppi", "[display]")
{
    const float ppi = compute_display_ppi(1.0f);
    INFO("1x scale should produce 96 ppi");
    REQUIRE(ppi == 96.0f);
}

TEST_CASE("dpi 2.0x scale produces 192 ppi", "[display]")
{
    const float ppi = compute_display_ppi(2.0f);
    INFO("2x scale should produce 192 ppi");
    REQUIRE(ppi == 192.0f);
}

TEST_CASE("dpi 1.5x scale produces 144 ppi", "[display]")
{
    const float ppi = compute_display_ppi(1.5f);
    INFO("1.5x scale should produce 144 ppi");
    REQUIRE(ppi == 144.0f);
}

// --- Grid dimensions via compute_panel_layout ---
// compute_panel_layout(pixel_w, pixel_h, cell_w, cell_h, padding, visible)
// grid_cols = (pixel_w - 2*padding) / cell_w
// grid_rows = (pixel_h - 2*padding) / cell_h

TEST_CASE("dpi 1.0x: grid dimensions match window pixels divided by cell size", "[display]")
{
    // 1x: 800x600 window, 8x16 cells, 1px padding
    const PanelLayout layout = compute_panel_layout(800, 600, 8, 16, 1, false);
    // cols = (800 - 2) / 8 = 99
    // rows = (600 - 2) / 16 = 37
    INFO("1x scale cols match pixel division");
    REQUIRE(layout.grid_size.x == 99);
    INFO("1x scale rows match pixel division");
    REQUIRE(layout.grid_size.y == 37);
}

TEST_CASE("dpi 2.0x: same logical window with doubled cell size halves grid dimensions", "[display]")
{
    // 2x Retina: window is 1600x1200 pixels but cells are 16x32 (doubled)
    // This simulates the same logical 800x600 window at 2x pixel density
    const PanelLayout layout_1x = compute_panel_layout(800, 600, 8, 16, 1, false);
    const PanelLayout layout_2x = compute_panel_layout(1600, 1200, 16, 32, 1, false);
    // Both should yield approximately the same grid dimensions
    // 1x: cols=(800-2)/8=99, rows=(600-2)/16=37
    // 2x: cols=(1600-2)/16=99, rows=(1200-2)/32=37
    INFO("2x retina: same logical grid cols when pixel size and cell size both double");
    REQUIRE(layout_1x.grid_size.x == layout_2x.grid_size.x);
    INFO("2x retina: same logical grid rows when pixel size and cell size both double");
    REQUIRE(layout_1x.grid_size.y == layout_2x.grid_size.y);
}

TEST_CASE("dpi 1.5x fractional scale: grid dimensions round down correctly", "[display]")
{
    // 1.5x: window 1200x900 pixels, cells 12x24 (1.5x of 8x16)
    // cols = (1200 - 2) / 12 = 99 (1198/12 = 99.8... → 99)
    // rows = (900 - 2) / 24 = 37 (898/24 = 37.4... → 37)
    const PanelLayout layout = compute_panel_layout(1200, 900, 12, 24, 1, false);
    INFO("1.5x scale cols round down correctly");
    REQUIRE(layout.grid_size.x == 99);
    INFO("1.5x scale rows round down correctly");
    REQUIRE(layout.grid_size.y == 37);
}

TEST_CASE("dpi window resize with constant dpi recalculates grid dimensions", "[display]")
{
    // Same DPI (1x), window grows from 800x600 to 1000x700
    const PanelLayout small = compute_panel_layout(800, 600, 8, 16, 1, false);
    const PanelLayout large = compute_panel_layout(1000, 700, 8, 16, 1, false);
    // small: cols=(800-2)/8=99, rows=(600-2)/16=37
    // large: cols=(1000-2)/8=124, rows=(700-2)/16=43
    INFO("wider window gives more columns at same DPI");
    REQUIRE(large.grid_size.x > small.grid_size.x);
    INFO("taller window gives more rows at same DPI");
    REQUIRE(large.grid_size.y > small.grid_size.y);
    INFO("wider window columns at 1x DPI");
    REQUIRE(large.grid_size.x == 124);
    INFO("taller window rows at 1x DPI");
    REQUIRE(large.grid_size.y == 43);
}

// --- TextService: cell metrics scale with PPI ---

TEST_CASE("dpi 2.0x ppi produces larger cell dimensions than 1.0x ppi", "[display]")
{
    auto font_path = bundled_font_path();
    if (!std::filesystem::exists(font_path))
        SKIP("bundled font not available");

    TextService svc_1x, svc_2x;
    TextServiceConfig cfg;
    cfg.font_path = font_path.string();

    INFO("1x text service initializes");
    REQUIRE(svc_1x.initialize(cfg, 11, compute_display_ppi(1.0f)));
    INFO("2x text service initializes");
    REQUIRE(svc_2x.initialize(cfg, 11, compute_display_ppi(2.0f)));

    const auto& m1 = svc_1x.metrics();
    const auto& m2 = svc_2x.metrics();

    INFO("2x ppi produces wider cells than 1x ppi");
    REQUIRE(m2.cell_width > m1.cell_width);
    INFO("2x ppi produces taller cells than 1x ppi");
    REQUIRE(m2.cell_height > m1.cell_height);

    svc_1x.shutdown();
    svc_2x.shutdown();
}

TEST_CASE("dpi 1.5x ppi produces cell dimensions between 1.0x and 2.0x", "[display]")
{
    auto font_path = bundled_font_path();
    if (!std::filesystem::exists(font_path))
        SKIP("bundled font not available");

    TextService svc_1x, svc_15x, svc_2x;
    TextServiceConfig cfg;
    cfg.font_path = font_path.string();

    INFO("1x text service initializes");
    REQUIRE(svc_1x.initialize(cfg, 11, compute_display_ppi(1.0f)));
    INFO("1.5x text service initializes");
    REQUIRE(svc_15x.initialize(cfg, 11, compute_display_ppi(1.5f)));
    INFO("2x text service initializes");
    REQUIRE(svc_2x.initialize(cfg, 11, compute_display_ppi(2.0f)));

    const auto& m1 = svc_1x.metrics();
    const auto& m15 = svc_15x.metrics();
    const auto& m2 = svc_2x.metrics();

    INFO("1.5x ppi cell height is not smaller than 1x");
    REQUIRE(m15.cell_height >= m1.cell_height);
    INFO("1.5x ppi cell height is not larger than 2x");
    REQUIRE(m15.cell_height <= m2.cell_height);

    svc_1x.shutdown();
    svc_15x.shutdown();
    svc_2x.shutdown();
}

TEST_CASE("dpi cell size at 1.0x feeds correct grid dimensions to panel layout", "[display]")
{
    auto font_path = bundled_font_path();
    if (!std::filesystem::exists(font_path))
        SKIP("bundled font not available");

    TextService svc;
    TextServiceConfig cfg;
    cfg.font_path = font_path.string();
    INFO("text service initializes");
    REQUIRE(svc.initialize(cfg, 11, compute_display_ppi(1.0f)));

    const auto& m = svc.metrics();
    INFO("cell width is positive");
    REQUIRE(m.cell_width > 0);
    INFO("cell height is positive");
    REQUIRE(m.cell_height > 0);

    // A typical 1x HD window: 1280x800
    const PanelLayout layout = compute_panel_layout(1280, 800, m.cell_width, m.cell_height, 1, false);
    INFO("grid has at least one column");
    REQUIRE(layout.grid_size.x > 0);
    INFO("grid has at least one row");
    REQUIRE(layout.grid_size.y > 0);
    // Columns should fit roughly 1280/cell_width characters
    const int expected_cols = (1280 - 2) / m.cell_width;
    const int expected_rows = (800 - 2) / m.cell_height;
    INFO("grid cols match expected cell division");
    REQUIRE(layout.grid_size.x == expected_cols);
    INFO("grid rows match expected cell division");
    REQUIRE(layout.grid_size.y == expected_rows);

    svc.shutdown();
}

TEST_CASE("dpi hotplug stress: 20 rapid scale changes produce consistent final state", "[display]")
{
    auto font_path = bundled_font_path();
    if (!std::filesystem::exists(font_path))
        SKIP("bundled font not available");

    // Simulate 20 rapid DPI changes by reinitializing with alternating scales.
    // The terminal must produce consistent state after each change.
    const std::vector<float> scales = {
        1.0f, 2.0f, 1.5f, 1.0f, 2.0f, 1.25f, 1.75f, 1.0f, 2.0f, 1.5f,
        1.0f, 1.5f, 2.0f, 1.0f, 1.25f, 1.75f, 2.0f, 1.5f, 1.0f, 2.0f
    };

    TextService svc;
    TextServiceConfig cfg;
    cfg.font_path = font_path.string();

    for (size_t i = 0; i < scales.size(); ++i)
    {
        // Reinitialise for each "hotplug" event
        if (i > 0)
            svc.shutdown();
        INFO("text service reinitialises after scale change");
        REQUIRE(svc.initialize(cfg, 11, compute_display_ppi(scales[i])));
        const auto& m = svc.metrics();
        INFO("cell width positive after scale change");
        REQUIRE(m.cell_width > 0);
        INFO("cell height positive after scale change");
        REQUIRE(m.cell_height > 0);
    }

    // Final state should match the last scale (2.0x)
    const float expected_ppi = compute_display_ppi(2.0f);
    TextService reference;
    INFO("reference service initializes at 2x");
    REQUIRE(reference.initialize(cfg, 11, expected_ppi));
    INFO("final state cell_width matches fresh 2x init");
    REQUIRE(svc.metrics().cell_width == reference.metrics().cell_width);
    INFO("final state cell_height matches fresh 2x init");
    REQUIRE(svc.metrics().cell_height == reference.metrics().cell_height);

    svc.shutdown();
    reference.shutdown();
}

TEST_CASE("dpi hotplug stress: panel layout consistent after each scale change", "[display]")
{
    // Simulate 20 rapid DPI changes via compute_panel_layout calls
    // (pure geometry, no font init needed)
    const std::vector<float> scales = {
        1.0f, 2.0f, 1.5f, 1.0f, 2.0f, 1.25f, 1.75f, 1.0f, 2.0f, 1.5f,
        1.0f, 1.5f, 2.0f, 1.0f, 1.25f, 1.75f, 2.0f, 1.5f, 1.0f, 2.0f
    };

    int last_cols = -1;
    int last_rows = -1;
    for (size_t i = 0; i < scales.size(); ++i)
    {
        const float scale = scales[i];
        const int px_w = static_cast<int>(1280 * scale);
        const int px_h = static_cast<int>(800 * scale);
        const int cell_w = static_cast<int>(8 * scale);
        const int cell_h = static_cast<int>(16 * scale);
        const PanelLayout layout = compute_panel_layout(px_w, px_h, cell_w, cell_h, 1, false);
        INFO("grid_cols positive after rapid scale change");
        REQUIRE(layout.grid_size.x > 0);
        INFO("grid_rows positive after rapid scale change");
        REQUIRE(layout.grid_size.y > 0);
        last_cols = layout.grid_size.x;
        last_rows = layout.grid_size.y;
    }
    // Final state is 2x scale; cols/rows should match a fresh 2x layout
    const PanelLayout final_layout = compute_panel_layout(1280 * 2, 800 * 2, 8 * 2, 16 * 2, 1, false);
    INFO("final cols after 20 hotplug events matches fresh 2x layout");
    REQUIRE(last_cols == final_layout.grid_size.x);
    INFO("final rows after 20 hotplug events matches fresh 2x layout");
    REQUIRE(last_rows == final_layout.grid_size.y);
}
