
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

// A fake glyph atlas that simulates overflow after a configurable number of
// resolve_cluster calls. When overflow is triggered, the atlas sets a reset
// flag that must be consumed (just like the real GlyphAtlasManager).
// Multiple consumers (pipelines) can observe the same reset: the flag stays
// true until consumed, and a "generation" counter tracks whether a given
// pipeline has seen the latest reset.
class MultiHostFakeAtlas final : public IGlyphAtlas
{
public:
    // overflow_after: trigger a reset after this many resolve_cluster calls.
    // Set to 0 to disable overflow simulation.
    explicit MultiHostFakeAtlas(int overflow_after = 0)
        : overflow_after_(overflow_after)
    {
        atlas_.assign(16, 0x7F);
        glyphs_["A"] = { { 0.0f, 0.0f, 0.25f, 0.5f }, { 1, 2 }, { 7, 9 }, false };
        glyphs_["B"] = { { 0.25f, 0.0f, 0.5f, 0.5f }, { 2, 3 }, { 8, 10 }, false };
        glyphs_["C"] = { { 0.5f, 0.0f, 0.75f, 0.5f }, { 1, 1 }, { 6, 8 }, false };
        glyphs_["D"] = { { 0.75f, 0.0f, 1.0f, 0.5f }, { 1, 2 }, { 7, 9 }, false };
    }

    AtlasRegion resolve_cluster(const std::string& text, bool /*is_bold*/, bool /*is_italic*/) override
    {
        ++total_resolve_calls;
        atlas_dirty_ = true;

        // Check if this resolve triggers an overflow reset.
        if (overflow_after_ > 0 && !reset_triggered_)
        {
            ++resolves_since_last_reset_;
            if (resolves_since_last_reset_ >= overflow_after_)
            {
                atlas_reset_pending_ = true;
                reset_triggered_ = true;
                ++reset_generation_;
            }
        }

        auto it = glyphs_.find(text);
        if (it != glyphs_.end())
            return it->second;
        return {};
    }

    int ligature_cell_span(const std::string& /*text*/, bool /*is_bold*/, bool /*is_italic*/) override
    {
        return 0;
    }

    bool atlas_dirty() const override
    {
        return atlas_dirty_;
    }

    bool consume_atlas_reset() override
    {
        if (!atlas_reset_pending_)
            return false;
        atlas_reset_pending_ = false;
        return true;
    }

    void clear_atlas_dirty() override
    {
        atlas_dirty_ = false;
    }

    const uint8_t* atlas_data() const override
    {
        return atlas_.data();
    }

    int atlas_width() const override
    {
        return 2;
    }

    int atlas_height() const override
    {
        return 2;
    }

    AtlasDirtyRect atlas_dirty_rect() const override
    {
        return { { 0, 0 }, { 2, 2 } };
    }

    // Force a pending reset externally (simulates overflow detected between flushes).
    void force_reset()
    {
        atlas_reset_pending_ = true;
        ++reset_generation_;
    }

    int total_resolve_calls = 0;
    int reset_generation_ = 0;

private:
    int overflow_after_ = 0;
    int resolves_since_last_reset_ = 0;
    bool reset_triggered_ = false;
    bool atlas_reset_pending_ = false;
    bool atlas_dirty_ = false;
    std::vector<uint8_t> atlas_;
    std::unordered_map<std::string, AtlasRegion> glyphs_;
};

// Fake grid handle that records cell update batches.
class FakeGridHandle final : public IGridHandle
{
public:
    void set_grid_size(int, int) override {}
    void update_cells(std::span<const CellUpdate> updates) override
    {
        update_batches.emplace_back(updates.begin(), updates.end());
    }
    void set_overlay_cells(std::span<const CellUpdate>) override {}
    void set_cursor(int, int, const CursorStyle&) override {}
    void set_default_background(Color) override {}
    void set_scroll_offset(float) override {}
    void set_viewport(const PaneDescriptor&) override {}

    std::vector<std::vector<CellUpdate>> update_batches;

    // Total number of cell updates across all batches.
    size_t total_cell_updates() const
    {
        size_t total = 0;
        for (const auto& batch : update_batches)
            total += batch.size();
        return total;
    }
};

class FakeRenderer final : public IGridRenderer
{
public:
    bool initialize(IWindow&) override
    {
        return true;
    }

    void shutdown() override {}
    bool begin_frame() override
    {
        return true;
    }
    void end_frame() override {}
    std::unique_ptr<IGridHandle> create_grid_handle() override
    {
        return std::make_unique<FakeGridHandle>();
    }

    void set_atlas_texture(const uint8_t*, int, int) override
    {
        ++full_atlas_uploads;
    }

    void update_atlas_region(int, int, int, int, const uint8_t*) override
    {
        ++region_uploads;
    }

    void resize(int, int) override {}

    std::pair<int, int> cell_size_pixels() const override
    {
        return { 10, 20 };
    }

    void set_cell_size(int, int) override {}
    void set_ascender(int) override {}
    int padding() const override
    {
        return 1;
    }

    void set_default_background(Color) override {}
    void register_render_pass(std::shared_ptr<IRenderPass>) override {}
    void unregister_render_pass() override {}
    void set_3d_viewport(int, int, int, int) override {}

    int full_atlas_uploads = 0;
    int region_uploads = 0;
};

} // namespace

TEST_CASE("atlas overflow multi-host: both handles receive cell updates after atlas reset", "[grid][multi-host]")
{
    // Set up two grids with different content, both backed by the same atlas.
    Grid grid_a;
    grid_a.resize(2, 1);
    grid_a.clear_dirty();
    grid_a.set_cell(0, 0, "A", 0, false);
    grid_a.set_cell(1, 0, "B", 0, false);

    Grid grid_b;
    grid_b.resize(2, 1);
    grid_b.clear_dirty();
    grid_b.set_cell(0, 0, "C", 0, false);
    grid_b.set_cell(1, 0, "D", 0, false);

    HighlightTable highlights;
    MultiHostFakeAtlas atlas;
    FakeRenderer renderer;
    FakeGridHandle handle_a;
    FakeGridHandle handle_b;

    GridRenderingPipeline pipeline_a(grid_a, highlights, atlas);
    pipeline_a.set_renderer(&renderer);
    pipeline_a.set_grid_handle(&handle_a);

    GridRenderingPipeline pipeline_b(grid_b, highlights, atlas);
    pipeline_b.set_renderer(&renderer);
    pipeline_b.set_grid_handle(&handle_b);

    // Force an atlas reset to simulate overflow.
    atlas.force_reset();

    // Flush pipeline A first. It should detect the reset, re-dirty its grid,
    // and produce cell updates on the retry.
    pipeline_a.flush();

    INFO("pipeline A should produce cell updates after atlas reset");
    REQUIRE(handle_a.update_batches.size() >= 1);
    INFO("pipeline A should update both cells");
    REQUIRE(handle_a.total_cell_updates() == 2);
    INFO("pipeline A cell 0 should have correct glyph from re-rasterisation");
    REQUIRE(handle_a.update_batches.back()[0].glyph.size.x == 7); // "A" glyph

    // Pipeline A consumed the reset. Pipeline B flushes next.
    // Since the reset was already consumed, pipeline B should still flush its
    // dirty cells normally (they were set dirty by set_cell above).
    pipeline_b.flush();

    INFO("pipeline B should produce cell updates");
    REQUIRE(handle_b.update_batches.size() >= 1);
    INFO("pipeline B should update both cells");
    REQUIRE(handle_b.total_cell_updates() == 2);
    INFO("pipeline B cell 0 should have correct glyph");
    REQUIRE(handle_b.update_batches.back()[0].glyph.size.x == 6); // "C" glyph
    INFO("pipeline B cell 1 should have correct glyph");
    REQUIRE(handle_b.update_batches.back()[1].glyph.size.x == 7); // "D" glyph
}

TEST_CASE("atlas overflow multi-host: reset during pipeline A flush does not corrupt pipeline B", "[grid][multi-host]")
{
    // Simulate overflow happening mid-flush of pipeline A:
    // the atlas overflows after 2 resolve calls (i.e. during pipeline A's first pass).
    Grid grid_a;
    grid_a.resize(2, 1);
    grid_a.clear_dirty();
    grid_a.set_cell(0, 0, "A", 0, false);
    grid_a.set_cell(1, 0, "B", 0, false);

    Grid grid_b;
    grid_b.resize(2, 1);
    grid_b.clear_dirty();
    grid_b.set_cell(0, 0, "C", 0, false);
    grid_b.set_cell(1, 0, "D", 0, false);

    HighlightTable highlights;
    // Overflow after 2 resolves -- during pipeline A's first pass.
    MultiHostFakeAtlas atlas(2);
    FakeRenderer renderer;
    FakeGridHandle handle_a;
    FakeGridHandle handle_b;

    GridRenderingPipeline pipeline_a(grid_a, highlights, atlas);
    pipeline_a.set_renderer(&renderer);
    pipeline_a.set_grid_handle(&handle_a);

    GridRenderingPipeline pipeline_b(grid_b, highlights, atlas);
    pipeline_b.set_renderer(&renderer);
    pipeline_b.set_grid_handle(&handle_b);

    // Pipeline A resolves 2 glyphs, which triggers the overflow.
    // The flush loop detects consume_atlas_reset() == true, retries once,
    // and succeeds on the second attempt.
    pipeline_a.flush();

    INFO("pipeline A should succeed after retry (4 resolve calls: 2 first pass + 2 retry)");
    REQUIRE(handle_a.update_batches.size() == 1);
    REQUIRE(handle_a.total_cell_updates() == 2);
    // 2 from first pass + 2 from retry = 4 total
    INFO("pipeline A should have resolved 4 times (2 + 2 retry)");
    REQUIRE(atlas.total_resolve_calls == 4);

    int resolves_before_b = atlas.total_resolve_calls;

    // Pipeline B flushes after. The reset was consumed by pipeline A,
    // so pipeline B proceeds normally with its dirty cells.
    pipeline_b.flush();

    INFO("pipeline B should produce cell updates");
    REQUIRE(handle_b.update_batches.size() >= 1);
    INFO("pipeline B should update both cells");
    REQUIRE(handle_b.total_cell_updates() == 2);
    INFO("pipeline B should resolve its 2 cells normally");
    REQUIRE(atlas.total_resolve_calls == resolves_before_b + 2);
    INFO("pipeline B cell 0 should have correct glyph");
    REQUIRE(handle_b.update_batches.back()[0].glyph.size.x == 6); // "C"
    INFO("pipeline B cell 1 should have correct glyph");
    REQUIRE(handle_b.update_batches.back()[1].glyph.size.x == 7); // "D"
}

TEST_CASE("atlas overflow multi-host: app-layer marks all grids dirty after reset", "[grid][multi-host]")
{
    // Scenario: both pipelines have already flushed (grids are clean), then
    // an atlas reset occurs. In the real app, the caller (App) marks all grids
    // dirty and forces full atlas uploads before flushing. This test verifies
    // that pattern produces correct re-uploads for both handles.
    Grid grid_a;
    grid_a.resize(2, 1);
    grid_a.clear_dirty();
    grid_a.set_cell(0, 0, "A", 0, false);
    grid_a.set_cell(1, 0, "B", 0, false);

    Grid grid_b;
    grid_b.resize(2, 1);
    grid_b.clear_dirty();
    grid_b.set_cell(0, 0, "C", 0, false);
    grid_b.set_cell(1, 0, "D", 0, false);

    HighlightTable highlights;
    MultiHostFakeAtlas atlas;
    FakeRenderer renderer;
    FakeGridHandle handle_a;
    FakeGridHandle handle_b;

    GridRenderingPipeline pipeline_a(grid_a, highlights, atlas);
    pipeline_a.set_renderer(&renderer);
    pipeline_a.set_grid_handle(&handle_a);

    GridRenderingPipeline pipeline_b(grid_b, highlights, atlas);
    pipeline_b.set_renderer(&renderer);
    pipeline_b.set_grid_handle(&handle_b);

    // Initial flush -- both pipelines render their cells.
    pipeline_a.flush();
    pipeline_b.flush();

    INFO("initial flush: both handles should have their cells");
    REQUIRE(handle_a.total_cell_updates() == 2);
    REQUIRE(handle_b.total_cell_updates() == 2);

    // Grids are now clean (dirty cleared by flush).
    REQUIRE(grid_a.dirty_cell_count() == 0);
    REQUIRE(grid_b.dirty_cell_count() == 0);

    // Clear tracking state for the second round.
    handle_a.update_batches.clear();
    handle_b.update_batches.clear();
    atlas.total_resolve_calls = 0;
    renderer.full_atlas_uploads = 0;

    // Simulate what the app layer does on atlas reset (e.g. font size change):
    // mark all grids dirty and force full atlas uploads on every pipeline.
    grid_a.mark_all_dirty();
    grid_b.mark_all_dirty();
    pipeline_a.force_full_atlas_upload();
    pipeline_b.force_full_atlas_upload();

    // Both pipelines flush with all cells dirty.
    pipeline_a.flush();
    pipeline_b.flush();

    INFO("pipeline A should re-upload all cells");
    REQUIRE(handle_a.total_cell_updates() == 2);
    INFO("pipeline A cell 0 should have correct glyph after re-rasterisation");
    REQUIRE(handle_a.update_batches.back()[0].glyph.size.x == 7); // "A"
    INFO("pipeline A cell 1 should have correct glyph after re-rasterisation");
    REQUIRE(handle_a.update_batches.back()[1].glyph.size.x == 8); // "B"

    INFO("pipeline B should re-upload all cells");
    REQUIRE(handle_b.total_cell_updates() == 2);
    INFO("pipeline B cell 0 should have correct glyph after re-rasterisation");
    REQUIRE(handle_b.update_batches.back()[0].glyph.size.x == 6); // "C"
    INFO("pipeline B cell 1 should have correct glyph after re-rasterisation");
    REQUIRE(handle_b.update_batches.back()[1].glyph.size.x == 7); // "D"

    INFO("full atlas uploads should have occurred for both pipelines");
    REQUIRE(renderer.full_atlas_uploads >= 1);
}

TEST_CASE("atlas overflow multi-host: concurrent dirty cells across handles are all re-uploaded", "[grid][multi-host]")
{
    // Both grids are dirty simultaneously when the atlas resets.
    Grid grid_a;
    grid_a.resize(3, 1);
    grid_a.clear_dirty();
    grid_a.set_cell(0, 0, "A", 0, false);
    grid_a.set_cell(1, 0, "B", 0, false);
    grid_a.set_cell(2, 0, "C", 0, false);

    Grid grid_b;
    grid_b.resize(2, 1);
    grid_b.clear_dirty();
    grid_b.set_cell(0, 0, "D", 0, false);
    grid_b.set_cell(1, 0, "A", 0, false);

    HighlightTable highlights;
    MultiHostFakeAtlas atlas;
    FakeRenderer renderer;
    FakeGridHandle handle_a;
    FakeGridHandle handle_b;

    GridRenderingPipeline pipeline_a(grid_a, highlights, atlas);
    pipeline_a.set_renderer(&renderer);
    pipeline_a.set_grid_handle(&handle_a);

    GridRenderingPipeline pipeline_b(grid_b, highlights, atlas);
    pipeline_b.set_renderer(&renderer);
    pipeline_b.set_grid_handle(&handle_b);

    // Force atlas reset while both grids have dirty cells.
    atlas.force_reset();

    pipeline_a.flush();
    pipeline_b.flush();

    INFO("handle A should receive all 3 cells");
    REQUIRE(handle_a.total_cell_updates() == 3);

    INFO("handle B should receive all 2 cells");
    REQUIRE(handle_b.total_cell_updates() == 2);

    // Verify specific glyph data to ensure re-rasterisation produced correct results.
    const auto& batch_a = handle_a.update_batches.back();
    INFO("handle A cell 0 has correct glyph (A)");
    REQUIRE(batch_a[0].glyph.size.x == 7);
    INFO("handle A cell 1 has correct glyph (B)");
    REQUIRE(batch_a[1].glyph.size.x == 8);
    INFO("handle A cell 2 has correct glyph (C)");
    REQUIRE(batch_a[2].glyph.size.x == 6);

    const auto& batch_b = handle_b.update_batches.back();
    INFO("handle B cell 0 has correct glyph (D)");
    REQUIRE(batch_b[0].glyph.size.x == 7);
    INFO("handle B cell 1 has correct glyph (A)");
    REQUIRE(batch_b[1].glyph.size.x == 7);
}
