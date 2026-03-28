
#include "support/fake_glyph_atlas.h"
#include "support/fake_grid_pipeline_renderer.h"

#include <draxul/grid_rendering_pipeline.h>

#include <draxul/grid.h>
#include <draxul/highlight.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <catch2/catch_all.hpp>

using namespace draxul;

namespace
{
using draxul::tests::FakeGlyphAtlas;
using draxul::tests::FakeGridPipelineHandle;
using draxul::tests::FakeGridPipelineRenderer;

Grid make_grid()
{
    Grid grid;
    grid.resize(2, 1);
    grid.clear_dirty();
    grid.set_cell(0, 0, "A", 0, false);
    grid.set_cell(1, 0, "B", 0, false);
    return grid;
}

Grid make_ligature_grid()
{
    Grid grid;
    grid.resize(2, 1);
    grid.clear_dirty();
    grid.set_cell(0, 0, "-", 0, false);
    grid.set_cell(1, 0, ">", 0, false);
    return grid;
}

} // namespace

TEST_CASE("grid rendering pipeline retries once after an atlas reset", "[grid]")
{
    Grid grid = make_grid();
    HighlightTable highlights;
    FakeGlyphAtlas atlas(1);
    FakeGridPipelineRenderer renderer;
    FakeGridPipelineHandle handle;
    GridRenderingPipeline pipeline(grid, highlights, atlas);
    pipeline.set_renderer(&renderer);
    pipeline.set_grid_handle(&handle);

    pipeline.flush();

    INFO("dirty cells should be replayed after the first atlas reset");
    REQUIRE(atlas.resolve_calls == 4);
    INFO("both glyphs should be resolved twice");
    REQUIRE(static_cast<int>(atlas.resolved_texts.size()) == 4);
    INFO("first pass keeps the first glyph");
    REQUIRE(atlas.resolved_texts[0] == std::string("A"));
    INFO("first pass keeps the second glyph");
    REQUIRE(atlas.resolved_texts[1] == std::string("B"));
    INFO("retry replays the first glyph");
    REQUIRE(atlas.resolved_texts[2] == std::string("A"));
    INFO("retry replays the second glyph");
    REQUIRE(atlas.resolved_texts[3] == std::string("B"));
    INFO("retry should force one full atlas upload");
    REQUIRE(renderer.full_atlas_uploads == 1);
    INFO("retry should not fall back to a region upload");
    REQUIRE(renderer.region_uploads == 0);
    INFO("successful retry should emit one cell update batch");
    REQUIRE(static_cast<int>(handle.update_batches.size()) == 1);
    INFO("both dirty cells should be sent to the renderer");
    REQUIRE(static_cast<int>(handle.update_batches[0].size()) == 2);
    INFO("first glyph survives the retry");
    REQUIRE(handle.update_batches[0][0].glyph.size.x == 7);
    INFO("second glyph survives the retry");
    REQUIRE(handle.update_batches[0][1].glyph.size.x == 8);
    INFO("successful retry clears the grid dirty set");
    REQUIRE(grid.dirty_cell_count() == size_t(0));
}

TEST_CASE("grid rendering pipeline gives up after a second atlas reset", "[grid]")
{
    Grid grid = make_grid();
    HighlightTable highlights;
    FakeGlyphAtlas atlas(2);
    FakeGridPipelineRenderer renderer;
    FakeGridPipelineHandle handle;
    GridRenderingPipeline pipeline(grid, highlights, atlas);
    pipeline.set_renderer(&renderer);
    pipeline.set_grid_handle(&handle);

    pipeline.flush();

    INFO("the retry loop should stop after two attempts");
    REQUIRE(atlas.resolve_calls == 4);
    INFO("double reset should not emit partial cell updates");
    REQUIRE(static_cast<int>(handle.update_batches.size()) == 0);
    INFO("no atlas upload happens when both attempts reset");
    REQUIRE(renderer.full_atlas_uploads == 0);
    INFO("cells stay dirty so the next flush can retry later");
    REQUIRE(grid.dirty_cell_count() == size_t(2));
}

TEST_CASE("grid rendering pipeline combines two-cell ligatures into a leader and blank continuation", "[grid]")
{
    Grid grid = make_ligature_grid();
    HighlightTable highlights;
    FakeGlyphAtlas atlas;
    FakeGridPipelineRenderer renderer;
    FakeGridPipelineHandle handle;
    GridRenderingPipeline pipeline(grid, highlights, atlas);
    pipeline.set_renderer(&renderer);
    pipeline.set_grid_handle(&handle);
    pipeline.set_enable_ligatures(true);

    pipeline.flush();

    INFO("two-cell ligature should resolve as one combined cluster");
    REQUIRE(atlas.resolve_calls == 1);
    INFO("only the combined ligature text should be resolved");
    REQUIRE(static_cast<int>(atlas.resolved_texts.size()) == 1);
    INFO("combined ligature text should be shaped");
    REQUIRE(atlas.resolved_texts[0] == std::string("->"));
    INFO("ligature flush emits one update batch");
    REQUIRE(static_cast<int>(handle.update_batches.size()) == 1);
    INFO("leader and continuation cells should both be updated");
    REQUIRE(static_cast<int>(handle.update_batches[0].size()) == 2);
    INFO("leader cell stores the ligature atlas region");
    REQUIRE(handle.update_batches[0][0].glyph.size.x == 18);
    INFO("continuation cell renders no glyph");
    REQUIRE(handle.update_batches[0][1].glyph.size.x == 0);
}

TEST_CASE("grid rendering pipeline redraws the leader when a continuation change breaks a ligature", "[grid]")
{
    Grid grid = make_ligature_grid();
    HighlightTable highlights;
    FakeGlyphAtlas atlas;
    FakeGridPipelineRenderer renderer;
    FakeGridPipelineHandle handle;
    GridRenderingPipeline pipeline(grid, highlights, atlas);
    pipeline.set_renderer(&renderer);
    pipeline.set_grid_handle(&handle);
    pipeline.set_enable_ligatures(true);

    pipeline.flush();

    atlas.resolve_calls = 0;
    atlas.resolved_texts.clear();
    handle.update_batches.clear();

    grid.set_cell(1, 0, "X", 0, false);
    pipeline.flush();

    INFO("breaking a ligature should redraw both participating cells");
    REQUIRE(atlas.resolve_calls == 2);
    INFO("broken ligature should fall back to per-cell shaping");
    REQUIRE(static_cast<int>(atlas.resolved_texts.size()) == 2);
    INFO("leader cell is replayed after the ligature breaks");
    REQUIRE(atlas.resolved_texts[0] == std::string("-"));
    INFO("changed continuation cell is shaped normally");
    REQUIRE(atlas.resolved_texts[1] == std::string("X"));
    INFO("fallback draw emits one update batch");
    REQUIRE(static_cast<int>(handle.update_batches.size()) == 1);
    INFO("both cells are updated to clear the old ligature");
    REQUIRE(static_cast<int>(handle.update_batches[0].size()) == 2);
    INFO("leader redraw restores a standalone glyph");
    REQUIRE(handle.update_batches[0][0].glyph.size.x > 0);
    INFO("changed cell restores its standalone glyph");
    REQUIRE(handle.update_batches[0][1].glyph.size.x > 0);
}

TEST_CASE("grid rendering pipeline reuses scratch capacity for ligature expansion", "[grid]")
{
    Grid grid = make_ligature_grid();
    HighlightTable highlights;
    FakeGlyphAtlas atlas;
    FakeGridPipelineRenderer renderer;
    FakeGridPipelineHandle handle;
    GridRenderingPipeline pipeline(grid, highlights, atlas);
    pipeline.set_renderer(&renderer);
    pipeline.set_grid_handle(&handle);
    pipeline.set_enable_ligatures(true);

    pipeline.flush();
    const size_t first_capacity = pipeline.expanded_scratch_capacity_for_testing();
    REQUIRE(first_capacity >= 6);

    grid.mark_all_dirty();
    pipeline.flush();

    CHECK(pipeline.expanded_scratch_capacity_for_testing() == first_capacity);
    REQUIRE(static_cast<int>(handle.update_batches.size()) == 2);
}
