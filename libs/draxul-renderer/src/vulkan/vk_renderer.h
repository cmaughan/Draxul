#pragma once
#include "vk_atlas.h"
#include "vk_buffers.h"
#include "vk_context.h"
#include "vk_pipeline.h"
#include <draxul/renderer.h>
#include <draxul/renderer_state.h>
#include <optional>

namespace draxul
{

// VkGridHandle is fully defined in vk_renderer.cpp.
class VkGridHandle;

class VkRenderer : public IRenderer
{
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    explicit VkRenderer(int atlas_size = kAtlasSize);

    bool initialize(IWindow& window) override;
    void shutdown() override;
    bool begin_frame() override;
    void end_frame() override;
    std::unique_ptr<IGridHandle> create_grid_handle() override;
    void set_atlas_texture(const uint8_t* data, int w, int h) override;
    void update_atlas_region(int x, int y, int w, int h, const uint8_t* data) override;
    void resize(int pixel_w, int pixel_h) override;
    std::pair<int, int> cell_size_pixels() const override;
    void set_cell_size(int w, int h) override;
    void set_ascender(int a) override;
    bool initialize_imgui_backend() override;
    void shutdown_imgui_backend() override;
    void rebuild_imgui_font_texture() override;
    void begin_imgui_frame() override;
    void set_imgui_draw_data(const ImDrawData* draw_data) override;
    void request_frame_capture() override;
    std::optional<CapturedFrame> take_captured_frame() override;
    int padding() const override
    {
        return padding_;
    }
    void set_default_background(Color bg) override;

    // I3DRenderer
    void register_render_pass(std::shared_ptr<IRenderPass> pass) override;
    void unregister_render_pass() override;
    void set_3d_viewport(int x, int y, int w, int h) override;

private:
    friend class VkGridHandle;

    // Internal single-pane grid methods (called by VkGridHandle).
    void set_grid_size(int cols, int rows);
    void update_cells(std::span<const CellUpdate> updates);
    void set_overlay_cells(std::span<const CellUpdate> updates);
    void set_cursor(int col, int row, const CursorStyle& style);
    void set_state_background(Color bg);

    bool create_sync_objects();
    bool create_command_buffers();
    bool create_descriptor_pool(const VkPipelineManager& pipeline, VkDescriptorPool& pool,
        std::vector<VkDescriptorSet>& bg_desc_sets, std::vector<VkDescriptorSet>& fg_desc_sets);
    bool recreate_frame_resources();
    void update_descriptor_sets_for_frame(VkDescriptorSet bg_desc_set, VkDescriptorSet fg_desc_set);
    void update_all_descriptor_sets();
    void record_command_buffer(VkCommandBuffer cmd, uint32_t image_index);
    void upload_dirty_state();
    bool flush_pending_atlas_uploads(VkCommandBuffer cmd);
    bool ensure_capture_buffer(size_t required_size);
    void destroy_capture_buffer();
    void finish_capture_readback();
    bool create_imgui_descriptor_pool();
    bool create_imgui_font_texture();

    int atlas_size_ = kAtlasSize;
    VkContext ctx_;
    VkPipelineManager pipeline_;
    VkAtlas atlas_;
    VkGridBuffer grid_buffer_;

    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmd_buffers_;
    std::vector<VkSemaphore> image_available_sem_;
    std::vector<VkSemaphore> render_finished_sem_;
    std::vector<VkFence> in_flight_fences_;

    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> bg_desc_sets_;
    std::vector<VkDescriptorSet> fg_desc_sets_;

    uint32_t current_frame_ = 0;
    uint32_t current_image_ = 0;
    bool framebuffer_resized_ = false;

    float clear_r_ = 0.1f;
    float clear_g_ = 0.1f;
    float clear_b_ = 0.1f;
    float scroll_offset_px_ = 0.0f;

    int cell_w_ = 10;
    int cell_h_ = 20;
    int ascender_ = 16;
    int padding_ = 4;
    int pixel_w_ = 0;
    int pixel_h_ = 0;

    RendererState state_;
    std::vector<PendingAtlasUpload> pending_atlas_uploads_;
    bool needs_descriptor_update_ = true;
    uint32_t desc_update_pending_frames_ = 0;
    bool capture_requested_ = false;
    std::optional<CapturedFrame> captured_frame_;
    VkBuffer capture_buffer_ = VK_NULL_HANDLE;
    VmaAllocation capture_allocation_ = VK_NULL_HANDLE;
    void* capture_mapped_ = nullptr;
    size_t capture_buffer_size_ = 0;
    VkDescriptorPool imgui_desc_pool_ = VK_NULL_HANDLE;
    const ImDrawData* imgui_draw_data_ = nullptr;
    bool imgui_initialized_ = false;

    std::shared_ptr<IRenderPass> render_pass_;
};

} // namespace draxul
