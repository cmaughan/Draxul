// Regression tests for ligature grouping and cluster pitch: typing text like
// "--render-test" or runs of '/' must not displace or corrupt earlier glyphs.
#include <catch2/catch_all.hpp>

#include "support/fake_glyph_atlas.h"
#include "support/fake_grid_pipeline_renderer.h"

#include <draxul/grid.h>
#include <draxul/grid_rendering_pipeline.h>
#include <draxul/highlight.h>
#include <draxul/text_service.h>

#include <cmath>
#include <filesystem>
#include <set>

using namespace draxul;

namespace
{

using draxul::tests::FakeGlyphAtlas;
using draxul::tests::FakeGridPipelineHandle;
using draxul::tests::FakeGridPipelineRenderer;

std::filesystem::path repo_root()
{
    auto here = std::filesystem::path(__FILE__).parent_path();
    return here.parent_path();
}

TextService make_service()
{
    TextServiceConfig config;
    config.font_path = (repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf").string();
    config.enable_ligatures = true;

    TextService service;
    REQUIRE(service.initialize(config, 11.0f, 192.0f));
    return service;
}

int leftmost_ink_column(const TextService& service, const AtlasRegion& region, int from_col, int to_col)
{
    const uint8_t* atlas = service.atlas_data();
    const int aw = service.atlas_width();
    const int ax = (int)std::lround(region.uv.x * aw);
    const int ay = (int)std::lround(region.uv.y * aw);
    for (int col = from_col; col < to_col; col++)
        for (int row = 0; row < region.bitmap_size.y; row++)
            if (atlas[(((size_t)(ay + row) * aw) + ax + col) * 4 + 3] > 0)
                return col;
    return -1;
}

} // namespace

TEST_CASE("ligature span covers only the cells whose shaping changed")
{
    TextService service = make_service();

    // Real ligatures keep their full span.
    CHECK(service.ligature_cell_span("=>") == 2);
    CHECK(service.ligature_cell_span("--") == 2);
    CHECK(service.ligature_cell_span("-->") == 3);

    // A candidate that merely *contains* a ligature pair must not swallow the
    // unrelated trailing cells ("--rend" happens while typing "--render-test").
    CHECK(service.ligature_cell_span("--rend") == 2);
    CHECK(service.ligature_cell_span("--r") == 2);

    // If the leading cell does not participate in the shaping change there is
    // no ligature anchored at it.
    CHECK(service.ligature_cell_span("xy=>") == 0);

    // Plain letter pairs never ligate.
    CHECK(service.ligature_cell_span("de") == 0);
    CHECK(service.ligature_cell_span("re") == 0);
}

TEST_CASE("multi-cell clusters are rasterized at grid cell pitch")
{
    TextService service = make_service();
    const int cell_w = service.metrics().cell_width;

    // 'a' and 'b' do not ligate, so the cluster preserves each glyph's own
    // shape; the second glyph must sit exactly one grid cell (not one font
    // advance) to the right of the first.
    AtlasRegion single_b = service.resolve_cluster("b");
    AtlasRegion pair = service.resolve_cluster("ab");
    REQUIRE(pair.bitmap_size.x >= 2 * cell_w);

    const int b_ink_single = leftmost_ink_column(service, single_b, 0, single_b.bitmap_size.x);
    const int b_ink_in_pair = leftmost_ink_column(service, pair, cell_w, pair.bitmap_size.x);
    REQUIRE(b_ink_single >= 0);
    REQUIRE(b_ink_in_pair >= 0);
    CHECK(b_ink_in_pair == cell_w + b_ink_single);
}

TEST_CASE("regrouping after an edit is anchored at the shaping run start")
{
    // 10-cell run of '/' so a fixed-radius expansion (the old behaviour)
    // would start re-grouping mid-run and emit clusters that overlap the
    // existing ones.
    Grid grid;
    grid.resize(11, 1);
    grid.clear_dirty();
    for (int c = 0; c < 9; c++)
        grid.set_cell(c, 0, "/", 0, false);

    HighlightTable highlights;
    FakeGlyphAtlas atlas;
    atlas.register_glyph("/", { { 0.0f, 0.0f, 0.1f, 0.1f }, { 1, 2 }, { 7, 9 }, 0, false });
    atlas.register_glyph("//", { { 0.1f, 0.0f, 0.2f, 0.1f }, { 1, 2 }, { 18, 9 }, 0, false });
    atlas.register_glyph("///", { { 0.2f, 0.0f, 0.3f, 0.1f }, { 1, 2 }, { 27, 9 }, 0, false });
    atlas.set_ligature_span("//", 2);
    atlas.set_ligature_span("///", 3);

    FakeGridPipelineRenderer renderer;
    FakeGridPipelineHandle handle;
    GridRenderingPipeline pipeline(grid, highlights, atlas);
    pipeline.set_renderer(&renderer);
    pipeline.set_grid_handle(&handle);
    pipeline.set_enable_ligatures(true);

    pipeline.flush();
    REQUIRE(handle.update_batches.size() == 1);

    // Type the 10th '/': only column 9 becomes dirty.
    grid.set_cell(9, 0, "/", 0, false);
    pipeline.flush();
    REQUIRE(handle.update_batches.size() == 2);

    // The whole run must be re-emitted, grouped from column 0: leaders at
    // 0, 3, 6 with three-cell clusters and a final standalone '/' at 9.
    std::set<int> leader_cols;
    std::set<int> emitted_cols;
    for (const auto& update : handle.update_batches[1])
    {
        emitted_cols.insert(update.col);
        if (update.glyph.bitmap_size.x > 0)
            leader_cols.insert(update.col);
    }

    CHECK(leader_cols == std::set<int>{ 0, 3, 6, 9 });
    for (int c = 0; c <= 9; c++)
        CHECK(emitted_cols.count(c) == 1);
}

TEST_CASE("editing the cell after a run still regroups the run")
{
    // "ab" where "ab" is a fake 2-cell ligature; typing a character after it
    // must not leave the pair stale if its grouping changes, and deleting the
    // trailing cell of a run must regroup what remains.
    Grid grid;
    grid.resize(4, 1);
    grid.clear_dirty();
    grid.set_cell(0, 0, "/", 0, false);
    grid.set_cell(1, 0, "/", 0, false);
    grid.set_cell(2, 0, "/", 0, false);

    HighlightTable highlights;
    FakeGlyphAtlas atlas;
    atlas.register_glyph("/", { { 0.0f, 0.0f, 0.1f, 0.1f }, { 1, 2 }, { 7, 9 }, 0, false });
    atlas.register_glyph("//", { { 0.1f, 0.0f, 0.2f, 0.1f }, { 1, 2 }, { 18, 9 }, 0, false });
    atlas.register_glyph("///", { { 0.2f, 0.0f, 0.3f, 0.1f }, { 1, 2 }, { 27, 9 }, 0, false });
    atlas.set_ligature_span("//", 2);
    atlas.set_ligature_span("///", 3);

    FakeGridPipelineRenderer renderer;
    FakeGridPipelineHandle handle;
    GridRenderingPipeline pipeline(grid, highlights, atlas);
    pipeline.set_renderer(&renderer);
    pipeline.set_grid_handle(&handle);
    pipeline.set_enable_ligatures(true);

    pipeline.flush();
    REQUIRE(handle.update_batches.size() == 1);

    // Delete the last '/': the remaining "//" must regroup from "///".
    grid.set_cell(2, 0, " ", 0, false);
    pipeline.flush();
    REQUIRE(handle.update_batches.size() == 2);

    std::set<int> leader_cols;
    std::set<int> emitted_cols;
    for (const auto& update : handle.update_batches[1])
    {
        emitted_cols.insert(update.col);
        if (update.glyph.bitmap_size.x > 0)
            leader_cols.insert(update.col);
    }

    // Column 0 leads a "//" cluster; columns 0..2 are all re-emitted.
    CHECK(leader_cols == std::set<int>{ 0 });
    CHECK(emitted_cols.count(0) == 1);
    CHECK(emitted_cols.count(1) == 1);
    CHECK(emitted_cols.count(2) == 1);
}
