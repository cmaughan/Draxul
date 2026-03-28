
#include <catch2/catch_all.hpp>

#include "support/fake_glyph_atlas.h"
#include "support/fake_grid_pipeline_renderer.h"

#include <draxul/grid_rendering_pipeline.h>
#include <draxul/ui_request_worker_state.h>

#include <draxul/grid.h>
#include <draxul/highlight.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace draxul;

// ---------------------------------------------------------------------------
// Minimal fakes reused from grid_rendering_pipeline_tests — kept local so this
// file compiles standalone and does not introduce a shared dependency.
// ---------------------------------------------------------------------------

namespace
{
using draxul::tests::FakeGlyphAtlas;
using draxul::tests::FakeGridPipelineHandle;
using draxul::tests::FakeGridPipelineRenderer;

// Build a grid with N cells all dirty
Grid make_populated_grid(int cols, int rows)
{
    Grid grid;
    grid.resize(cols, rows);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            grid.set_cell(c, r, "A", 0, false);
    return grid;
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// ------------------------------------------------------------------
// Step 1 — grid.mark_all_dirty() marks every cell
// ------------------------------------------------------------------
TEST_CASE("font size change: mark_all_dirty marks every cell in the grid", "[font]")
{
    Grid grid;
    grid.resize(10, 5);
    grid.clear_dirty();

    INFO("newly cleared grid should have no dirty cells");
    REQUIRE(grid.dirty_cell_count() == size_t(0));

    grid.mark_all_dirty();

    INFO("mark_all_dirty should dirty all 10x5 cells");
    REQUIRE(grid.dirty_cell_count() == size_t(50));
}

TEST_CASE("font size change: mark_all_dirty on a single-row grid marks every column", "[font]")
{
    Grid grid;
    grid.resize(80, 1);
    grid.clear_dirty();
    grid.mark_all_dirty();

    INFO("mark_all_dirty should mark all 80 columns in a single-row grid");
    REQUIRE(grid.dirty_cell_count() == size_t(80));
}

// ------------------------------------------------------------------
// Step 2 — force_full_atlas_upload causes a full atlas upload on next flush
// ------------------------------------------------------------------
TEST_CASE("font size change: force_full_atlas_upload triggers a full atlas texture upload on next flush", "[font]")
{
    // Set up a 2x1 grid with two known cells so the pipeline has work to do.
    Grid grid;
    grid.resize(2, 1);
    grid.set_cell(0, 0, "A", 0, false);
    grid.set_cell(1, 0, "B", 0, false);

    HighlightTable highlights;
    FakeGlyphAtlas atlas;
    atlas.register_glyph("A", { { 0.0f, 0.0f, 0.25f, 0.5f }, { 1, 2 }, { 7, 9 }, false });
    atlas.register_glyph("B", { { 0.25f, 0.0f, 0.5f, 0.5f }, { 2, 3 }, { 8, 10 }, false });

    FakeGridPipelineRenderer renderer;
    renderer.padding_pixels = 0;
    FakeGridPipelineHandle handle;
    GridRenderingPipeline pipeline(grid, highlights, atlas);
    pipeline.set_renderer(&renderer);
    pipeline.set_grid_handle(&handle);

    // Perform a first flush to settle the baseline state.
    pipeline.flush();
    const int uploads_after_first_flush = renderer.full_atlas_uploads;

    // Simulate what change_font_size does: mark all cells dirty and force
    // a full atlas upload.
    grid.mark_all_dirty();
    pipeline.force_full_atlas_upload();
    pipeline.flush();

    INFO("force_full_atlas_upload should produce at least one additional full atlas texture upload");
    REQUIRE(renderer.full_atlas_uploads > uploads_after_first_flush);
}

TEST_CASE("font size change: force_full_atlas_upload with no dirty cells still schedules a full upload", "[font]")
{
    Grid grid;
    grid.resize(2, 1);
    grid.set_cell(0, 0, "A", 0, false);
    grid.set_cell(1, 0, "B", 0, false);

    HighlightTable highlights;
    FakeGlyphAtlas atlas;
    atlas.register_glyph("A", { { 0.0f, 0.0f, 0.25f, 0.5f }, { 1, 2 }, { 7, 9 }, false });
    atlas.register_glyph("B", { { 0.25f, 0.0f, 0.5f, 0.5f }, { 2, 3 }, { 8, 10 }, false });

    FakeGridPipelineRenderer renderer;
    renderer.padding_pixels = 0;
    FakeGridPipelineHandle handle;
    GridRenderingPipeline pipeline(grid, highlights, atlas);
    pipeline.set_renderer(&renderer);
    pipeline.set_grid_handle(&handle);

    // First flush clears dirty state.
    pipeline.flush();
    const int uploads_after_first_flush = renderer.full_atlas_uploads;

    // force_full_atlas_upload with no pending dirty cells — next flush
    // should still issue the full upload.
    pipeline.force_full_atlas_upload();
    pipeline.flush();

    INFO("force_full_atlas_upload should schedule the upload even when no cells are dirty");
    REQUIRE(renderer.full_atlas_uploads > uploads_after_first_flush);
}

// ------------------------------------------------------------------
// Step 3 — UiRequestWorkerState queues a resize with correct dimensions
// ------------------------------------------------------------------
TEST_CASE("font size change: UiRequestWorkerState queues resize with new dimensions", "[font]")
{
    UiRequestWorkerState state;
    state.start();

    // Simulate change_font_size issuing a resize after recalculating layout
    const int new_cols = 100;
    const int new_rows = 30;
    const bool accepted = state.request_resize(new_cols, new_rows, "font resize");

    INFO("running worker state should accept a resize request");
    REQUIRE(accepted);
    INFO("resize request should be pending");
    REQUIRE(state.has_pending_request());

    auto pending = state.take_pending_request();
    INFO("pending request should be retrievable");
    REQUIRE(pending.has_value());
    INFO("queued resize should carry the new column count");
    REQUIRE(pending->cols == new_cols);
    INFO("queued resize should carry the new row count");
    REQUIRE(pending->rows == new_rows);
    INFO("queued resize should preserve the originating reason");
    REQUIRE(pending->reason == std::string("font resize"));
}

TEST_CASE("font size change: UiRequestWorkerState coalesces rapid font size changes to the latest resize", "[font]")
{
    UiRequestWorkerState state;
    state.start();

    // Simulate user holding down font-increase shortcut — several rapid
    // font size changes each compute different new dimensions.
    state.request_resize(80, 24, "font resize");
    state.request_resize(78, 23, "font resize");
    state.request_resize(76, 22, "font resize");

    auto pending = state.take_pending_request();
    INFO("there should be exactly one pending request after rapid changes");
    REQUIRE(pending.has_value());
    INFO("coalescing should keep the last submitted column count");
    REQUIRE(pending->cols == 76);
    INFO("coalescing should keep the last submitted row count");
    REQUIRE(pending->rows == 22);
}

TEST_CASE("font size change: UiRequestWorkerState does not queue a resize when stopped", "[font]")
{
    UiRequestWorkerState state;
    state.start();
    state.stop();

    const bool accepted = state.request_resize(80, 24, "font resize");
    INFO("stopped worker state should reject resize requests");
    REQUIRE(!accepted);
    INFO("no request should be pending after rejection");
    REQUIRE(!state.has_pending_request());
}

// ------------------------------------------------------------------
// Combined cascade: mark_all_dirty + force_full_atlas_upload together
// ------------------------------------------------------------------
TEST_CASE("font size change cascade: dirty grid plus forced atlas upload produces full re-render on flush", "[font]")
{
    // Replicate the full change_font_size cascade steps that don't
    // require a live TextService or IWindow:
    //   1. grid_.mark_all_dirty()
    //   2. grid_pipeline_.force_full_atlas_upload()
    //   3. grid_pipeline_.flush()   (→ full atlas upload + cell updates)
    //   4. UiRequestWorkerState::request_resize(new_cols, new_rows)

    const int cols = 8;
    const int rows = 3;

    Grid grid = make_populated_grid(cols, rows);
    grid.clear_dirty();

    HighlightTable highlights;
    FakeGlyphAtlas atlas;
    atlas.register_glyph("A", { { 0.0f, 0.0f, 0.25f, 0.5f }, { 1, 2 }, { 7, 9 }, false });

    FakeGridPipelineRenderer renderer;
    renderer.padding_pixels = 0;
    FakeGridPipelineHandle handle;
    GridRenderingPipeline pipeline(grid, highlights, atlas);
    pipeline.set_renderer(&renderer);
    pipeline.set_grid_handle(&handle);

    // Baseline flush — clears dirty state, establishes initial uploads.
    pipeline.flush();
    const int baseline_uploads = renderer.full_atlas_uploads;
    const int baseline_resolves = atlas.resolve_calls;

    // --- Font size change cascade ---

    // Step 1: mark all cells dirty (mirrors grid_.mark_all_dirty())
    grid.mark_all_dirty();
    INFO("all cells should be dirty after mark_all_dirty");
    REQUIRE(grid.dirty_cell_count() == size_t(cols * rows));

    // Step 2: flag forced full atlas upload (mirrors grid_pipeline_.force_full_atlas_upload())
    pipeline.force_full_atlas_upload();

    // Step 3: flush (mirrors grid_pipeline_.flush() in change_font_size)
    pipeline.flush();

    INFO("cascade flush should issue a full atlas texture upload");
    REQUIRE(renderer.full_atlas_uploads > baseline_uploads);
    INFO("cascade flush should re-resolve glyphs for all dirty cells");
    REQUIRE(atlas.resolve_calls > baseline_resolves);
    INFO("grid should have no dirty cells after a successful cascade flush");
    REQUIRE(grid.dirty_cell_count() == size_t(0));

    // Step 4: resize RPC request (mirrors queue_resize_request in change_font_size)
    const int new_cols = cols - 1; // smaller font → more columns, but we use any change
    const int new_rows = rows - 1;
    UiRequestWorkerState worker_state;
    worker_state.start();
    worker_state.request_resize(new_cols, new_rows, "font resize");

    auto pending = worker_state.take_pending_request();
    INFO("resize RPC should be queued after cascade");
    REQUIRE(pending.has_value());
    INFO("resize request carries correct column count");
    REQUIRE(pending->cols == new_cols);
    INFO("resize request carries correct row count");
    REQUIRE(pending->rows == new_rows);
}
