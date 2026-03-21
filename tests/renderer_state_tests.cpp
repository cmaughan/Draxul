
#include <draxul/renderer_state.h>

#include <vector>

#include <catch2/catch_all.hpp>

using namespace draxul;

namespace
{

CellUpdate make_cell_update(int col, int row, float bg_r, float fg_r)
{
    CellUpdate update;
    update.col = col;
    update.row = row;
    update.bg = { bg_r, bg_r + 0.1f, bg_r + 0.2f, 1.0f };
    update.fg = { fg_r, fg_r - 0.1f, fg_r - 0.2f, 1.0f };
    update.sp = update.fg;
    return update;
}

CursorStyle make_block_cursor()
{
    CursorStyle cursor;
    cursor.shape = CursorShape::Block;
    cursor.use_explicit_colors = true;
    cursor.fg = { 1.0f, 1.0f, 1.0f, 1.0f };
    cursor.bg = { 0.0f, 0.0f, 0.0f, 1.0f };
    return cursor;
}

std::vector<GpuCell> snapshot(const RendererState& state)
{
    std::vector<GpuCell> gpu(state.total_cells() + RendererState::OVERLAY_CELL_CAPACITY + 1);
    state.copy_to(gpu.data());
    return gpu;
}

} // namespace

TEST_CASE("gpu cell equality compares all meaningful fields", "[renderer]")
{
    GpuCell a = {};
    a.pos_x = 10.0f;
    a.pos_y = 20.0f;
    a.size_x = 11.0f;
    a.size_y = 21.0f;
    a.bg_r = 0.1f;
    a.bg_g = 0.2f;
    a.bg_b = 0.3f;
    a.bg_a = 0.4f;
    a.fg_r = 0.5f;
    a.fg_g = 0.6f;
    a.fg_b = 0.7f;
    a.fg_a = 0.8f;
    a.sp_r = 0.9f;
    a.sp_g = 1.0f;
    a.sp_b = 1.1f;
    a.sp_a = 1.2f;
    a.uv_x0 = 0.11f;
    a.uv_y0 = 0.22f;
    a.uv_x1 = 0.33f;
    a.uv_y1 = 0.44f;
    a.glyph_offset_x = 2.0f;
    a.glyph_offset_y = 3.0f;
    a.glyph_size_x = 4.0f;
    a.glyph_size_y = 5.0f;
    a.style_flags = 7;

    GpuCell b = a;
    INFO("gpu cell equality holds for identical cells");
    REQUIRE(a == b);

    GpuCell c = a;
    c.style_flags = 8;
    INFO("gpu cell equality tracks style_flags changes");
    REQUIRE(!(a == c));

    GpuCell d = a;
    d.pos_x = 99.0f;
    INFO("gpu cell equality tracks position changes");
    REQUIRE(!(a == d));
}

TEST_CASE("renderer state projects cell updates into gpu cells", "[renderer]")
{
    RendererState state;
    state.set_grid_size(2, 1, 1);
    state.set_cell_size(10, 20);
    state.set_ascender(16);

    CellUpdate update;
    update.col = 1;
    update.row = 0;
    update.bg = { 0.1f, 0.2f, 0.3f, 1.0f };
    update.fg = { 0.9f, 0.8f, 0.7f, 1.0f };
    update.sp = { 0.5f, 0.4f, 0.3f, 1.0f };
    update.glyph = { 0.1f, 0.2f, 0.3f, 0.4f, 2, 3, 6, 7 };
    update.style_flags = 5;
    state.update_cells({ &update, 1 });

    std::vector<GpuCell> gpu(state.total_cells() + RendererState::OVERLAY_CELL_CAPACITY + 1);
    state.copy_to(gpu.data());

    INFO("buffer includes overlay region and cursor slot");
    REQUIRE(static_cast<int>(state.buffer_size_bytes()) == static_cast<int>((state.total_cells() + RendererState::OVERLAY_CELL_CAPACITY + 1) * sizeof(GpuCell)));
    INFO("cell x position uses cell width and padding");
    REQUIRE(static_cast<int>(gpu[1].pos_x) == 11);
    INFO("cell y position uses padding");
    REQUIRE(static_cast<int>(gpu[1].pos_y) == 1);
    INFO("background color is copied");
    REQUIRE(gpu[1].bg_b == 0.3f);
    INFO("foreground color is copied");
    REQUIRE(gpu[1].fg_r == 0.9f);
    INFO("special color is copied");
    REQUIRE(gpu[1].sp_r == 0.5f);
    INFO("glyph width is copied");
    REQUIRE(gpu[1].glyph_size_x == 6.0f);
    INFO("glyph y offset uses ascender and cell height");
    REQUIRE(gpu[1].glyph_offset_y == 7.0f);
    INFO("style flags are copied");
    REQUIRE(gpu[1].style_flags == static_cast<uint32_t>(5));
}

TEST_CASE("block cursor saves the cell under the cursor for later restore", "[renderer]")
{
    RendererState state;
    state.set_grid_size(1, 1, 1);

    CellUpdate update = make_cell_update(0, 0, 0.2f, 0.8f);
    state.update_cells({ &update, 1 });

    state.set_cursor(0, 0, make_block_cursor());
    state.apply_cursor();

    auto gpu = snapshot(state);
    INFO("block cursor overrides background");
    REQUIRE(gpu[0].bg_r == 0.0f);
    INFO("block cursor overrides foreground");
    REQUIRE(gpu[0].fg_r == 1.0f);

    state.restore_cursor();
    gpu = snapshot(state);
    INFO("restoring cursor restores background");
    REQUIRE(gpu[0].bg_r == 0.2f);
    INFO("restoring cursor restores foreground");
    REQUIRE(gpu[0].fg_r == 0.8f);
}

TEST_CASE("block cursor restores the previous cell when the cursor moves", "[renderer]")
{
    RendererState state;
    state.set_grid_size(2, 1, 1);

    CellUpdate updates[] = {
        make_cell_update(0, 0, 0.2f, 0.8f),
        make_cell_update(1, 0, 0.4f, 0.6f),
    };
    state.update_cells(updates);

    const CursorStyle cursor = make_block_cursor();
    state.set_cursor(0, 0, cursor);
    state.apply_cursor();
    state.set_cursor(1, 0, cursor);
    state.apply_cursor();

    const auto gpu = snapshot(state);
    INFO("moving the cursor restores the old cell background");
    REQUIRE(gpu[0].bg_r == 0.2f);
    INFO("moving the cursor restores the old cell foreground");
    REQUIRE(gpu[0].fg_r == 0.8f);
    INFO("the new cursor location receives the cursor background");
    REQUIRE(gpu[1].bg_r == 0.0f);
    INFO("the new cursor location receives the cursor foreground");
    REQUIRE(gpu[1].fg_r == 1.0f);
}

TEST_CASE("block cursor restores the saved cell when hidden", "[renderer]")
{
    RendererState state;
    state.set_grid_size(1, 1, 1);

    CellUpdate update = make_cell_update(0, 0, 0.3f, 0.7f);
    state.update_cells({ &update, 1 });

    const CursorStyle cursor = make_block_cursor();
    state.set_cursor(0, 0, cursor);
    state.apply_cursor();
    state.set_cursor(-1, -1, cursor);

    const auto gpu = snapshot(state);
    INFO("hiding the cursor restores the original background");
    REQUIRE(gpu[0].bg_r == 0.3f);
    INFO("hiding the cursor restores the original foreground");
    REQUIRE(gpu[0].fg_r == 0.7f);
}

TEST_CASE("adjacent cell updates do not corrupt block cursor restore", "[renderer]")
{
    RendererState state;
    state.set_grid_size(2, 1, 1);

    CellUpdate original_updates[] = {
        make_cell_update(0, 0, 0.2f, 0.8f),
        make_cell_update(1, 0, 0.4f, 0.6f),
    };
    state.update_cells(original_updates);

    state.set_cursor(0, 0, make_block_cursor());
    state.apply_cursor();

    CellUpdate adjacent = make_cell_update(1, 0, 0.9f, 0.1f);
    state.update_cells({ &adjacent, 1 });

    const auto gpu = snapshot(state);
    INFO("restoring the cursor keeps the original cell content");
    REQUIRE(gpu[0].bg_r == 0.2f);
    INFO("restoring the cursor keeps the original foreground");
    REQUIRE(gpu[0].fg_r == 0.8f);
    INFO("adjacent updates are preserved");
    REQUIRE(gpu[1].bg_r == 0.9f);
    INFO("adjacent foreground updates are preserved");
    REQUIRE(gpu[1].fg_r == 0.1f);
}

TEST_CASE("renderer state appends overlay geometry for line cursors", "[renderer]")
{
    RendererState state;
    state.set_grid_size(1, 1, 1);
    state.set_cell_size(10, 20);

    CellUpdate update;
    update.col = 0;
    update.row = 0;
    state.update_cells({ &update, 1 });

    CursorStyle cursor;
    cursor.shape = CursorShape::Vertical;
    cursor.bg = { 1.0f, 0.0f, 0.0f, 1.0f };
    cursor.cell_percentage = 30;

    state.set_cursor(0, 0, cursor);
    state.apply_cursor();

    std::vector<GpuCell> gpu(state.total_cells() + RendererState::OVERLAY_CELL_CAPACITY + 1);
    state.copy_to(gpu.data());

    INFO("line cursor adds one overlay background instance");
    REQUIRE(state.bg_instances() == 2);
    INFO("overlay cell carries cursor background");
    REQUIRE(gpu[state.total_cells()].bg_r == 1.0f);
    INFO("overlay width uses cell percentage");
    REQUIRE(gpu[state.total_cells()].size_x == 3.0f);
    INFO("overlay height spans the cell");
    REQUIRE(gpu[state.total_cells()].size_y == 20.0f);
}

TEST_CASE("renderer state keeps debug overlay cells separate from the grid", "[renderer]")
{
    RendererState state;
    state.set_grid_size(2, 1, 1);
    state.set_cell_size(10, 20);
    state.set_ascender(16);
    state.clear_dirty();

    CellUpdate overlay;
    overlay.col = 1;
    overlay.row = 0;
    overlay.bg = { 0.0f, 0.0f, 0.0f, 0.8f };
    overlay.fg = { 0.1f, 0.8f, 1.0f, 1.0f };
    overlay.sp = overlay.fg;
    overlay.glyph = { 0.1f, 0.2f, 0.3f, 0.4f, 1, 2, 7, 8 };
    state.set_overlay_cells({ &overlay, 1 });

    std::vector<GpuCell> gpu(state.total_cells() + RendererState::OVERLAY_CELL_CAPACITY + 1);
    state.copy_to(gpu.data());

    INFO("debug overlay contributes a background instance");
    REQUIRE(state.bg_instances() == 3);
    INFO("debug overlay contributes a foreground instance");
    REQUIRE(state.fg_instances() == 3);
    INFO("setting the debug overlay dirties the overlay region");
    REQUIRE(state.overlay_region_dirty());
    INFO("overlay cell position uses grid coordinates");
    REQUIRE(gpu[state.total_cells()].pos_x == 11.0f);
    INFO("overlay cell foreground is copied");
    REQUIRE(gpu[state.total_cells()].fg_g == 0.8f);
}

TEST_CASE("renderer state tracks dirty ranges for incremental uploads", "[renderer]")
{
    RendererState state;
    state.set_grid_size(3, 1, 1);
    INFO("initial layout dirties the cell buffer");
    REQUIRE(state.has_dirty_cells());
    INFO("initial dirty range starts at zero");
    REQUIRE(static_cast<int>(state.dirty_cell_offset_bytes()) == 0);
    INFO("initial dirty range spans the grid");
    REQUIRE(static_cast<int>(state.dirty_cell_size_bytes()) == static_cast<int>(3 * sizeof(GpuCell)));
    INFO("initial layout dirties the overlay region too");
    REQUIRE(state.overlay_region_dirty());

    state.clear_dirty();

    CellUpdate update_a;
    update_a.col = 0;
    update_a.row = 0;
    CellUpdate update_b;
    update_b.col = 2;
    update_b.row = 0;
    CellUpdate updates[] = { update_a, update_b };
    state.update_cells(updates);

    INFO("cell updates produce a dirty range");
    REQUIRE(state.has_dirty_cells());
    INFO("dirty range starts at the first updated cell");
    REQUIRE(static_cast<int>(state.dirty_cell_offset_bytes()) == 0);
    INFO("dirty range expands to cover the touched span");
    REQUIRE(static_cast<int>(state.dirty_cell_size_bytes()) == static_cast<int>(3 * sizeof(GpuCell)));
    INFO("plain cell updates do not dirty the overlay region");
    REQUIRE(!state.overlay_region_dirty());

    CursorStyle cursor;
    cursor.shape = CursorShape::Vertical;
    cursor.bg = { 1.0f, 0.0f, 0.0f, 1.0f };
    cursor.cell_percentage = 25;
    state.set_cursor(1, 0, cursor);
    state.apply_cursor();

    INFO("overlay cursor dirties the overlay region");
    REQUIRE(state.overlay_region_dirty());
}
