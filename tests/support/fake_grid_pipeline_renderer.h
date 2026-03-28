#pragma once

#include <draxul/renderer.h>

#include <span>
#include <vector>

namespace draxul::tests
{

class FakeGridPipelineHandle final : public IGridHandle
{
public:
    void set_grid_size(int, int) override {}
    void update_cells(std::span<const CellUpdate> updates) override
    {
        update_batches.emplace_back(updates.begin(), updates.end());
    }
    void set_overlay_cells(std::span<const CellUpdate> updates) override
    {
        last_overlay.assign(updates.begin(), updates.end());
    }
    void set_cursor(int, int, const CursorStyle&) override {}
    void set_default_background(Color) override {}
    void set_scroll_offset(float) override {}
    void set_viewport(const PaneDescriptor&) override {}

    size_t total_cell_updates() const
    {
        size_t total = 0;
        for (const auto& batch : update_batches)
            total += batch.size();
        return total;
    }

    void reset()
    {
        update_batches.clear();
        last_overlay.clear();
    }

    std::vector<std::vector<CellUpdate>> update_batches;
    std::vector<CellUpdate> last_overlay;
};

class FakeGridPipelineRenderer final : public IGridRenderer
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
        auto handle = std::make_unique<FakeGridPipelineHandle>();
        last_handle = handle.get();
        return handle;
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
        return { cell_width_pixels, cell_height_pixels };
    }

    void set_cell_size(int, int) override {}
    void set_ascender(int) override {}

    int padding() const override
    {
        return padding_pixels;
    }

    void set_default_background(Color) override {}
    void register_render_pass(std::shared_ptr<IRenderPass>) override {}
    void unregister_render_pass() override {}
    void set_3d_viewport(int, int, int, int) override {}

    FakeGridPipelineHandle* last_handle = nullptr;
    int full_atlas_uploads = 0;
    int region_uploads = 0;
    int cell_width_pixels = 10;
    int cell_height_pixels = 20;
    int padding_pixels = 1;
};

} // namespace draxul::tests
