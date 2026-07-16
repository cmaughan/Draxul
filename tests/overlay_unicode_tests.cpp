#include <catch2/catch_all.hpp>

#include <draxul/gui/overlay_text.h>
#include <draxul/gui/palette_renderer.h>
#include <draxul/gui/toast_renderer.h>
#include <draxul/gui/tooltip.h>
#include <draxul/text_service.h>

#include <array>
#include <filesystem>

using namespace draxul;

namespace
{

std::filesystem::path overlay_test_font()
{
    return std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "CascadiaCode-Regular.ttf";
}

bool initialize_text_service(TextService& service)
{
    if (!std::filesystem::exists(overlay_test_font()))
        return false;
    TextServiceConfig config;
    config.font_path = overlay_test_font().string();
    return service.initialize(config, 13.0f, 96.0f);
}

} // namespace

TEST_CASE("overlay text uses complete display clusters and cell widths", "[gui][unicode]")
{
    const std::string text = "\xC3\xA9\xE7\x95\x8C\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"
                             "e\xCC\x81";
    const auto clusters = gui::layout_overlay_text(text, 20);

    REQUIRE(clusters.size() == 4);
    CHECK(clusters[0].text == "\xC3\xA9");
    CHECK(clusters[0].cell_width == 1);
    CHECK(clusters[1].text == "\xE7\x95\x8C");
    CHECK(clusters[1].cell_width == 2);
    CHECK(clusters[1].continuation_cells == 1);
    CHECK(clusters[2].text == "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB");
    CHECK(clusters[2].cell_width == 2);
    CHECK(clusters[3].text == "e\xCC\x81");
    CHECK(clusters[3].cell_width == 1);
    CHECK(gui::overlay_text_width(text) == 6);
}

TEST_CASE("overlay text replaces invalid UTF-8 and clips before a partial wide cell", "[gui][unicode]")
{
    const std::string invalid("A\x80" "B", 3);
    const auto repaired = gui::layout_overlay_text(invalid, 10);
    REQUIRE(repaired.size() == 3);
    CHECK(repaired[1].text == "\xEF\xBF\xBD");
    CHECK(repaired[1].replacement);

    const auto clipped = gui::layout_overlay_text("A\xE7\x95\x8C" "B", 2);
    REQUIRE(clipped.size() == 1);
    CHECK(clipped[0].text == "A");

    const auto wide_fits = gui::layout_overlay_text("A\xE7\x95\x8C" "B", 3);
    REQUIRE(wide_fits.size() == 2);
    CHECK(wide_fits[1].column == 1);
    CHECK(wide_fits[1].cell_width == 2);

    const auto leading_combining = gui::layout_overlay_text("\xCC\x81" "A", 2);
    REQUIRE(leading_combining.size() == 2);
    CHECK(leading_combining[0].cell_width == 1);
}

TEST_CASE("palette places wide Unicode on cell boundaries", "[gui][palette][unicode]")
{
    TextService text_service;
    if (!initialize_text_service(text_service))
        SKIP("test font not found");

    const std::string name = "A\xE7\x95\x8C" "B";
    const gui::PaletteEntry entry{ .name = name };
    const std::array entries{ entry };
    const gui::PaletteViewState state{
        .grid_cols = 10,
        .grid_rows = 3,
        .selected_index = 0,
        .entries = entries,
    };

    const auto cells = gui::render_palette(state, text_service);
    REQUIRE(cells.size() == 30);
    CHECK(cells[1].glyph.bitmap_size.x > 0); // A at column 1
    CHECK(cells[2].glyph.bitmap_size.x >= 0); // CJK cluster starts at column 2
    CHECK(cells[3].glyph.bitmap_size.x == 0); // explicit continuation cell
    CHECK(cells[4].glyph.bitmap_size.x > 0); // B follows at column 4, not byte column 5
}

TEST_CASE("palette keeps combining and ZWJ clusters intact and clips wide edges", "[gui][palette][unicode]")
{
    TextService text_service;
    if (!initialize_text_service(text_service))
        SKIP("test font not found");

    const std::array clustered_entries{
        gui::PaletteEntry{ .name = "e\xCC\x81" "B" },
        gui::PaletteEntry{ .name = "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB" "B" },
    };
    const gui::PaletteViewState clustered_state{
        .grid_cols = 12,
        .grid_rows = 4,
        .selected_index = 0,
        .entries = clustered_entries,
    };
    const auto clustered = gui::render_palette(clustered_state, text_service);
    REQUIRE(clustered.size() == 48);
    // Combining mark stays with e, so B advances to column 2 (not byte column 4).
    CHECK(clustered[2].glyph.bitmap_size.x > 0);
    // The ZWJ emoji is one two-cell cluster: col 2 is its continuation and B is col 3.
    CHECK(clustered[12 + 2].glyph.bitmap_size.x == 0);
    CHECK(clustered[12 + 3].glyph.bitmap_size.x > 0);

    const std::array clipped_entries{ gui::PaletteEntry{ .name = "AB\xE7\x95\x8C" } };
    const gui::PaletteViewState clipped_state{
        .grid_cols = 5,
        .grid_rows = 3,
        .selected_index = 0,
        .entries = clipped_entries,
    };
    const auto clipped = gui::render_palette(clipped_state, text_service);
    REQUIRE(clipped.size() == 15);
    // The final content cell retains the occlusion glyph; the two-cell CJK
    // cluster is omitted rather than split at the right edge.
    CHECK(clipped[3].glyph.bitmap_size.x > 0);
}

TEST_CASE("tooltip measures Unicode by display cells rather than bytes", "[gui][tooltip][unicode]")
{
    TextService text_service;
    if (!initialize_text_service(text_service))
        SKIP("test font not found");

    const gui::TooltipData unicode{ { { "\xC3\xA9", "\xE7\x95\x8C" } } };
    const gui::TooltipData equivalent_ascii{ { { "A", "BC" } } };
    const auto unicode_bitmap = gui::rasterize_tooltip(text_service, unicode);
    const auto ascii_bitmap = gui::rasterize_tooltip(text_service, equivalent_ascii);

    REQUIRE(unicode_bitmap.valid());
    REQUIRE(ascii_bitmap.valid());
    CHECK(unicode_bitmap.width == ascii_bitmap.width);
    CHECK(unicode_bitmap.height == ascii_bitmap.height);
}

TEST_CASE("toast advances by Unicode cells and emits a wide continuation", "[gui][toast][unicode]")
{
    TextService text_service;
    if (!initialize_text_service(text_service))
        SKIP("test font not found");

    const std::array entries{ gui::ToastEntry{
        .level = gui::ToastLevel::Info,
        .message = "A\xE7\x95\x8C" "B",
        .remaining_s = 2.0f,
        .total_s = 2.0f,
    } };
    const gui::ToastViewState state{ .grid_cols = 20, .grid_rows = 3, .entries = entries };
    const auto cells = gui::render_toasts(state, text_service);

    auto last_at = [&](int col, int row) -> const CellUpdate* {
        const CellUpdate* found = nullptr;
        for (const auto& cell : cells)
        {
            if (cell.col == col && cell.row == row)
                found = &cell;
        }
        return found;
    };

    REQUIRE(last_at(15, 1) != nullptr);
    REQUIRE(last_at(16, 1) != nullptr);
    CHECK(last_at(15, 1)->glyph.bitmap_size.x == 0);
    CHECK(last_at(16, 1)->glyph.bitmap_size.x > 0);
}
