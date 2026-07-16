#pragma once
#include "vk_atlas.h"
#include "vk_buffers.h"
#include "vk_context.h"
#include "vk_pipeline.h"
#include <array>
#include <draxul/renderer.h>
#include <optional>
#include <utility>

namespace draxul
{

// VkGridHandle is fully defined in vk_renderer.cpp.
class VkGridHandle;

class VkRenderer : public IGridRenderer, public IImGuiHost, public ICaptureRenderer
{
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    explicit VkRenderer(int atlas_size = kAtlasSize, RendererOptions options = {});

    bool initialize(IWindow& window) override;
    void shutdown() override;
    IFrameContext* begin_frame() override;
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
    void request_frame_capture() override;
    std::optional<CapturedFrame> take_captured_frame() override;
    int padding() const override
    {
        return padding_;
    }
    void set_default_background(Color bg) override;

private:
    struct RetiredGridSlotResources
    {
        VkGridBuffer::BufferState buffer;
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet bg_desc_set = VK_NULL_HANDLE;
        VkDescriptorSet fg_desc_set = VK_NULL_HANDLE;
    };

    class FrameContext;
    friend class VkGridHandle;

    bool create_sync_objects();
    bool resize_render_finished_semaphores(size_t count);
    bool create_command_buffers();
    bool create_descriptor_pool(VkDescriptorPool& pool);
    bool recreate_frame_resources();
    bool start_new_chunk_command_buffer();
    bool flush_submit_chunk(bool final_chunk);
    bool flush_pending_atlas_uploads(VkCommandBuffer cmd);
    void retire_grid_slot_resources(uint32_t frame_index, VkGridBuffer::BufferState buffer, VkDescriptorPool descriptor_pool,
        VkDescriptorSet bg_desc_set, VkDescriptorSet fg_desc_set);
    void reclaim_retired_grid_slot_resources(uint32_t frame_index);
    void reclaim_all_retired_grid_slot_resources();
    bool ensure_capture_buffer(size_t required_size);
    void destroy_capture_buffer();
    void finish_capture_readback();
    bool create_imgui_descriptor_pool();
    bool create_imgui_font_texture();
    bool begin_main_render_pass(bool clear_depth_for_load = false);
    void end_main_render_pass();
    bool draw_grid_handle_now(IGridHandle& handle);
    bool record_render_pass_now(IRenderPass& pass, const RenderViewport& viewport);
    bool render_imgui_now(const ImDrawData* draw_data, ImGuiContext* context);

    int atlas_size_ = kAtlasSize;
    VkContext ctx_;
    VkPipelineManager pipeline_;
    VkAtlas atlas_;

    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmd_buffers_;
    std::array<std::vector<VkCommandBuffer>, MAX_FRAMES_IN_FLIGHT> extra_cmd_buffers_;
    std::vector<VkSemaphore> image_available_sem_;
    std::vector<VkSemaphore> render_finished_sem_;
    std::vector<VkFence> in_flight_fences_;
    std::vector<VkFence> images_in_flight_;

    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;

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
    std::vector<PendingAtlasUpload> pending_atlas_uploads_;
    bool capture_requested_ = false;
    std::optional<CapturedFrame> captured_frame_;
    VkBuffer capture_buffer_ = VK_NULL_HANDLE;
    VmaAllocation capture_allocation_ = VK_NULL_HANDLE;
    void* capture_mapped_ = nullptr;
    size_t capture_buffer_size_ = 0;
    VkDescriptorPool imgui_desc_pool_ = VK_NULL_HANDLE;
    bool imgui_initialized_ = false;
    bool imgui_font_texture_rebuild_pending_ = false;
    std::vector<VkGridHandle*> grid_handles_;
    std::array<std::vector<RetiredGridSlotResources>, MAX_FRAMES_IN_FLIGHT> retired_grid_slot_resources_;
    std::unique_ptr<FrameContext> frame_context_;
    VkCommandBuffer active_cmd_buffer_ = VK_NULL_HANDLE;
    bool frame_active_ = false;
    bool main_render_pass_active_ = false;
    bool main_render_pass_started_ = false;
    bool chunk_has_work_ = false;
    uint32_t current_chunk_index_ = 0;
};

} // namespace draxul
