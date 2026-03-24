
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

class FakeGlyphAtlas final : public IGlyphAtlas
{
public:
    explicit FakeGlyphAtlas(int resets_remaining = 0)
        : resets_remaining_(resets_remaining)
    {
        atlas_.assign(16, 0x7F);
        glyphs_["A"] = { { 0.0f, 0.0f, 0.25f, 0.5f }, { 1, 2 }, { 7, 9 }, false };
        glyphs_["B"] = { { 0.25f, 0.0f, 0.5f, 0.5f }, { 2, 3 }, { 8, 10 }, false };
        glyphs_["-"] = { { 0.5f, 0.0f, 0.625f, 0.5f }, { 1, 1 }, { 6, 3 }, false };
        glyphs_[">"] = { { 0.625f, 0.0f, 0.75f, 0.5f }, { 1, 1 }, { 6, 8 }, false };
        glyphs_["X"] = { { 0.75f, 0.0f, 0.875f, 0.5f }, { 1, 2 }, { 7, 9 }, false };
        glyphs_["->"] = { { 0.0f, 0.5f, 0.375f, 1.0f }, { 1, 2 }, { 18, 9 }, false };
        ligature_spans_["->"] = 2;
    }

    AtlasRegion resolve_cluster(const std::string& text, bool /*is_bold*/, bool /*is_italic*/) override
    {
        ++resolve_calls;
        resolved_texts.push_back(text);
        atlas_dirty_ = true;

        auto it = glyphs_.find(text);
        if (it != glyphs_.end())
            return it->second;
        return {};
    }

    int ligature_cell_span(const std::string& text, bool /*is_bold*/, bool /*is_italic*/) override
    {
        auto it = ligature_spans_.find(text);
        return it != ligature_spans_.end() ? it->second : 0;
    }

    bool atlas_dirty() const override
    {
        return atlas_dirty_;
    }

    bool consume_atlas_reset() override
    {
        if (resets_remaining_ <= 0)
            return false;

        --resets_remaining_;
        atlas_dirty_ = false;
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

    int resolve_calls = 0;
    std::vector<std::string> resolved_texts;

private:
    int resets_remaining_ = 0;
    bool atlas_dirty_ = false;
    std::vector<uint8_t> atlas_;
    std::unordered_map<std::string, AtlasRegion> glyphs_;
    std::unordered_map<std::string, int> ligature_spans_;
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
    FakeRenderer renderer;
    FakeGridHandle handle;
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
    FakeRenderer renderer;
    FakeGridHandle handle;
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
    FakeRenderer renderer;
    FakeGridHandle handle;
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
    FakeRenderer renderer;
    FakeGridHandle handle;
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
    FakeRenderer renderer;
    FakeGridHandle handle;
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
