#include "vk_renderer.h"
#include "vk_render_context.h"

#include <algorithm>
#include <backends/imgui_impl_vulkan.h>
#include <cstdio>
#include <cstring>
#include <draxul/log.h>
#include <draxul/pane_descriptor.h>
#include <draxul/renderer_state.h>
#include <draxul/window.h>
#include <imgui.h>

namespace draxul
{

// ---------------------------------------------------------------------------
// VkGridHandle — per-host grid handle for the Vulkan backend.
// Each handle owns its pane-local cell state, viewport, and scroll offset.
// VkRenderer repacks all active handles into the current frame slot's SSBO.
// ---------------------------------------------------------------------------
class VkGridHandle final : public IGridHandle
{
public:
    VkGridHandle(VkRenderer& renderer, int padding)
        : renderer_(renderer)
        , padding_(padding)
    {
        renderer_.grid_handles_.push_back(this);
    }

    ~VkGridHandle() override
    {
        auto& handles = renderer_.grid_handles_;
        handles.erase(std::remove(handles.begin(), handles.end(), this), handles.end());
    }

    void set_grid_size(int cols, int rows) override
    {
        state_.set_grid_size(cols, rows, padding_);
    }
    void update_cells(std::span<const CellUpdate> updates) override
    {
        state_.update_cells(updates);
    }
    void set_overlay_cells(std::span<const CellUpdate> updates) override
    {
        state_.set_overlay_cells(updates);
    }
    void set_cursor(int col, int row, const CursorStyle& style) override
    {
        state_.set_cursor(col, row, style);
    }
    void set_default_background(Color bg) override
    {
        state_.set_default_background(bg);
    }
    void set_scroll_offset(float px) override
    {
        scroll_offset_px_ = px;
    }
    void set_viewport(const PaneDescriptor& desc) override
    {
        descriptor_ = desc;
    }

    RendererState state_;
    PaneDescriptor descriptor_;
    float scroll_offset_px_ = 0.0f;

private:
    VkRenderer& renderer_;
    int padding_ = 4;
};

namespace
{

void transition_image_layout(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
    VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

VkRenderer::VkRenderer(int atlas_size, RendererOptions options)
    : atlas_size_(atlas_size)
    , ctx_(options.wait_for_vblank)
{
}

void VkRenderer::upload_dirty_state()
{
    size_t total_size = 0;
    for (auto* handle : grid_handles_)
        total_size += handle->state_.buffer_size_bytes();

    if (total_size == 0)
        return;

    auto& grid_buffer = grid_buffers_[current_frame_];
    const auto resize_result = grid_buffer.ensure_size(ctx_.allocator(), total_size);
    if (resize_result == BufferResizeResult::Failed)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to resize Vulkan grid buffer to %zu bytes", total_size);
        return;
    }
    if (resize_result == BufferResizeResult::Resized)
        update_descriptor_sets_for_frame(current_frame_, bg_desc_sets_[current_frame_], fg_desc_sets_[current_frame_]);

    auto* mapped = static_cast<std::byte*>(grid_buffer.mapped());
    if (!mapped)
        return;

    size_t byte_offset = 0;
    for (auto* handle : grid_handles_)
    {
        handle->state_.copy_to(mapped + byte_offset);
        handle->state_.clear_dirty();
        byte_offset += handle->state_.buffer_size_bytes();
    }
    grid_buffer.flush_range(ctx_.allocator(), 0, total_size);
}

bool VkRenderer::ensure_capture_buffer(size_t required_size)
{
    if (required_size <= capture_buffer_size_ && capture_buffer_ != VK_NULL_HANDLE)
        return true;

    destroy_capture_buffer();

    VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_info.size = required_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocation_info = {};
    if (vmaCreateBuffer(ctx_.allocator(), &buffer_info, &alloc_info, &capture_buffer_, &capture_allocation_, &allocation_info) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create Vulkan readback buffer");
        return false;
    }

    capture_mapped_ = allocation_info.pMappedData;
    capture_buffer_size_ = required_size;
    return true;
}

void VkRenderer::destroy_capture_buffer()
{
    if (capture_buffer_ == VK_NULL_HANDLE)
        return;

    vmaDestroyBuffer(ctx_.allocator(), capture_buffer_, capture_allocation_);
    capture_buffer_ = VK_NULL_HANDLE;
    capture_allocation_ = VK_NULL_HANDLE;
    capture_mapped_ = nullptr;
    capture_buffer_size_ = 0;
}

void VkRenderer::finish_capture_readback()
{
    if (!capture_requested_ || capture_buffer_ == VK_NULL_HANDLE || capture_mapped_ == nullptr)
        return;

    vmaInvalidateAllocation(ctx_.allocator(), capture_allocation_, 0, VK_WHOLE_SIZE);

    const uint32_t width = ctx_.swapchain().extent.width;
    const uint32_t height = ctx_.swapchain().extent.height;
    CapturedFrame frame;
    frame.width = static_cast<int>(width);
    frame.height = static_cast<int>(height);
    frame.rgba.resize(static_cast<size_t>(width) * height * 4);

    const auto* src = static_cast<const uint8_t*>(capture_mapped_);
    for (size_t i = 0; i < frame.rgba.size(); i += 4)
    {
        frame.rgba[i + 0] = src[i + 2];
        frame.rgba[i + 1] = src[i + 1];
        frame.rgba[i + 2] = src[i + 0];
        frame.rgba[i + 3] = src[i + 3];
    }

    captured_frame_ = std::move(frame);
    capture_requested_ = false;
}

bool VkRenderer::initialize(IWindow& window)
{
    if (!ctx_.initialize(static_cast<SDL_Window*>(window.native_handle())))
        return false;

    auto [w, h] = window.size_pixels();
    pixel_w_ = w;
    pixel_h_ = h;

    if (!atlas_.initialize(ctx_, atlas_size_))
        return false;
    for (auto& grid_buffer : grid_buffers_)
    {
        if (!grid_buffer.initialize(ctx_, 80 * 24 * sizeof(GpuCell)))
            return false;
    }
    if (!pipeline_.initialize(ctx_.device(), ctx_.render_pass(), "shaders"))
        return false;
    if (!create_command_buffers())
        return false;
    if (!create_sync_objects())
        return false;
    if (!create_descriptor_pool(pipeline_, desc_pool_, bg_desc_sets_, fg_desc_sets_))
        return false;
    images_in_flight_.assign(ctx_.swapchain().images.size(), VK_NULL_HANDLE);
    update_all_descriptor_sets();
    return true;
}

void VkRenderer::register_render_pass(std::shared_ptr<IRenderPass> pass)
{
    render_pass_ = std::move(pass);
}

void VkRenderer::unregister_render_pass()
{
    render_pass_.reset();
}

void VkRenderer::set_3d_viewport(int x, int y, int w, int h)
{
    viewport3d_x_ = x;
    viewport3d_y_ = y;
    viewport3d_w_ = w;
    viewport3d_h_ = h;
}

void VkRenderer::shutdown()
{
    if (ctx_.device())
        vkDeviceWaitIdle(ctx_.device());

    for (auto& sem : image_available_sem_)
        vkDestroySemaphore(ctx_.device(), sem, nullptr);
    for (auto& sem : render_finished_sem_)
        vkDestroySemaphore(ctx_.device(), sem, nullptr);
    for (auto& fence : in_flight_fences_)
        vkDestroyFence(ctx_.device(), fence, nullptr);
    if (cmd_pool_)
        vkDestroyCommandPool(ctx_.device(), cmd_pool_, nullptr);
    if (desc_pool_)
        vkDestroyDescriptorPool(ctx_.device(), desc_pool_, nullptr);
    if (imgui_desc_pool_)
        vkDestroyDescriptorPool(ctx_.device(), imgui_desc_pool_, nullptr);
    destroy_capture_buffer();

    for (auto& grid_buffer : grid_buffers_)
        grid_buffer.shutdown(ctx_.allocator());
    pipeline_.shutdown(ctx_.device());
    atlas_.shutdown(ctx_);
    ctx_.shutdown();
}

bool VkRenderer::create_sync_objects()
{
    image_available_sem_.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_sem_.resize(MAX_FRAMES_IN_FLIGHT);
    in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo sem_ci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_ci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(ctx_.device(), &sem_ci, nullptr, &image_available_sem_[i]) != VK_SUCCESS)
            return false;
        if (vkCreateSemaphore(ctx_.device(), &sem_ci, nullptr, &render_finished_sem_[i]) != VK_SUCCESS)
            return false;
        if (vkCreateFence(ctx_.device(), &fence_ci, nullptr, &in_flight_fences_[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

bool VkRenderer::create_command_buffers()
{
    VkCommandPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pool_ci.queueFamilyIndex = ctx_.graphics_queue_family();
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(ctx_.device(), &pool_ci, nullptr, &cmd_pool_) != VK_SUCCESS)
        return false;

    cmd_buffers_.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc_info.commandPool = cmd_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    return vkAllocateCommandBuffers(ctx_.device(), &alloc_info, cmd_buffers_.data()) == VK_SUCCESS;
}

bool VkRenderer::create_descriptor_pool(const VkPipelineManager& pipeline, VkDescriptorPool& pool,
    std::vector<VkDescriptorSet>& bg_desc_sets, std::vector<VkDescriptorSet>& fg_desc_sets)
{
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)(MAX_FRAMES_IN_FLIGHT * 2) },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)MAX_FRAMES_IN_FLIGHT },
    };

    VkDescriptorPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pool_ci.maxSets = MAX_FRAMES_IN_FLIGHT * 2;
    pool_ci.poolSizeCount = 2;
    pool_ci.pPoolSizes = pool_sizes;
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if (vkCreateDescriptorPool(ctx_.device(), &pool_ci, nullptr, &pool) != VK_SUCCESS)
        return false;

    bg_desc_sets.resize(MAX_FRAMES_IN_FLIGHT);
    fg_desc_sets.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorSetLayout bg_layout = pipeline.bg_desc_layout();
        VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc_info.descriptorPool = pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &bg_layout;
        if (vkAllocateDescriptorSets(ctx_.device(), &alloc_info, &bg_desc_sets[i]) != VK_SUCCESS)
            return false;

        VkDescriptorSetLayout fg_layout = pipeline.fg_desc_layout();
        alloc_info.pSetLayouts = &fg_layout;
        if (vkAllocateDescriptorSets(ctx_.device(), &alloc_info, &fg_desc_sets[i]) != VK_SUCCESS)
            return false;
    }

    return true;
}

bool VkRenderer::recreate_frame_resources()
{
    PendingSwapchainResources pending_swapchain;
    if (!ctx_.build_swapchain_resources(pixel_w_, pixel_h_, pending_swapchain))
        return false;

    VkPipelineManager pending_pipeline;
    if (!pending_pipeline.initialize(ctx_.device(), pending_swapchain.render_pass, "shaders"))
    {
        ctx_.destroy_pending_swapchain_resources(pending_swapchain);
        return false;
    }

    VkDescriptorPool pending_desc_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> pending_bg_desc_sets;
    std::vector<VkDescriptorSet> pending_fg_desc_sets;
    if (!create_descriptor_pool(pending_pipeline, pending_desc_pool, pending_bg_desc_sets, pending_fg_desc_sets))
    {
        if (pending_desc_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(ctx_.device(), pending_desc_pool, nullptr);
        pending_pipeline.shutdown(ctx_.device());
        ctx_.destroy_pending_swapchain_resources(pending_swapchain);
        return false;
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        update_descriptor_sets_for_frame(static_cast<size_t>(i), pending_bg_desc_sets[(size_t)i], pending_fg_desc_sets[(size_t)i]);

    VkDescriptorPool old_desc_pool = desc_pool_;
    ctx_.commit_swapchain_resources(std::move(pending_swapchain));

    pipeline_.swap(pending_pipeline);
    pending_pipeline.shutdown(ctx_.device());

    desc_pool_ = pending_desc_pool;
    bg_desc_sets_ = std::move(pending_bg_desc_sets);
    fg_desc_sets_ = std::move(pending_fg_desc_sets);
    if (old_desc_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(ctx_.device(), old_desc_pool, nullptr);

    images_in_flight_.assign(ctx_.swapchain().images.size(), VK_NULL_HANDLE);

    if (imgui_initialized_ && ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData)
    {
        shutdown_imgui_backend();
        if (!initialize_imgui_backend())
            return false;
    }

    return true;
}

void VkRenderer::update_descriptor_sets_for_frame(size_t frame_index, VkDescriptorSet bg_desc_set, VkDescriptorSet fg_desc_set)
{
    auto& grid_buffer = grid_buffers_[frame_index];

    VkDescriptorBufferInfo buf_info = {};
    buf_info.buffer = grid_buffer.buffer();
    buf_info.offset = 0;
    buf_info.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet writes[3] = {};
    writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[0].dstSet = bg_desc_set;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &buf_info;

    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[1].dstSet = fg_desc_set;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &buf_info;

    VkDescriptorImageInfo img_info = {};
    img_info.sampler = atlas_.sampler();
    img_info.imageView = atlas_.image_view();
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[2].dstSet = fg_desc_set;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &img_info;

    vkUpdateDescriptorSets(ctx_.device(), 3, writes, 0, nullptr);
}

void VkRenderer::update_all_descriptor_sets()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        update_descriptor_sets_for_frame(static_cast<size_t>(i), bg_desc_sets_[(size_t)i], fg_desc_sets_[(size_t)i]);
}

void VkRenderer::set_atlas_texture(const uint8_t* data, int w, int h)
{
    queue_full_atlas_upload(pending_atlas_uploads_, data, w, h);
}

void VkRenderer::update_atlas_region(int x, int y, int w, int h, const uint8_t* data)
{
    queue_atlas_region_upload(pending_atlas_uploads_, x, y, w, h, data);
}

void VkRenderer::resize(int pixel_w, int pixel_h)
{
    pixel_w_ = pixel_w;
    pixel_h_ = pixel_h;
    framebuffer_resized_ = true;
}

std::pair<int, int> VkRenderer::cell_size_pixels() const
{
    return { cell_w_, cell_h_ };
}

void VkRenderer::set_cell_size(int w, int h)
{
    cell_w_ = w;
    cell_h_ = h;
    for (auto* handle : grid_handles_)
        handle->state_.set_cell_size(w, h);
}

void VkRenderer::set_ascender(int a)
{
    ascender_ = a;
    for (auto* handle : grid_handles_)
        handle->state_.set_ascender(a);
}

std::unique_ptr<IGridHandle> VkRenderer::create_grid_handle()
{
    return std::make_unique<VkGridHandle>(*this, padding_);
}

void VkRenderer::set_default_background(Color bg)
{
    clear_r_ = bg.r;
    clear_g_ = bg.g;
    clear_b_ = bg.b;
}

bool VkRenderer::create_imgui_descriptor_pool()
{
    if (imgui_desc_pool_ != VK_NULL_HANDLE)
        return true;

    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 64 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 64 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 64 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 64 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 64 },
    };

    VkDescriptorPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_ci.maxSets = 64 * static_cast<uint32_t>(std::size(pool_sizes));
    pool_ci.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
    pool_ci.pPoolSizes = pool_sizes;
    if (vkCreateDescriptorPool(ctx_.device(), &pool_ci, nullptr, &imgui_desc_pool_) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create ImGui Vulkan descriptor pool");
        return false;
    }

    return true;
}

bool VkRenderer::create_imgui_font_texture()
{
    const bool ok = ImGui_ImplVulkan_CreateFontsTexture();
    return ok;
}

void VkRenderer::rebuild_imgui_font_texture()
{
    if (!ImGui::GetCurrentContext() || !ImGui::GetIO().BackendRendererUserData)
        return;

    ImGui_ImplVulkan_DestroyFontsTexture();
    create_imgui_font_texture();
}

bool VkRenderer::initialize_imgui_backend()
{
    // Already initialized for the current ImGui context — nothing to do.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData)
        return true;

    // The descriptor pool is shared across all contexts; create it only once.
    if (!create_imgui_descriptor_pool())
        return false;

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = ctx_.instance();
    init_info.PhysicalDevice = ctx_.physical_device();
    init_info.Device = ctx_.device();
    init_info.QueueFamily = ctx_.graphics_queue_family();
    init_info.Queue = ctx_.graphics_queue();
    init_info.DescriptorPool = imgui_desc_pool_;
    init_info.RenderPass = ctx_.render_pass();
    init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
    init_info.ImageCount = static_cast<uint32_t>(ctx_.swapchain().images.size());
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&init_info))
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to initialize ImGui Vulkan backend");
        return false;
    }

    if (!create_imgui_font_texture())
    {
        ImGui_ImplVulkan_Shutdown();
        return false;
    }

    imgui_initialized_ = true;
    return true;
}

void VkRenderer::shutdown_imgui_backend()
{
    // Shuts down the backend for the current ImGui context only.
    if (!ImGui::GetCurrentContext() || !ImGui::GetIO().BackendRendererUserData)
        return;

    vkDeviceWaitIdle(ctx_.device());
    ImGui_ImplVulkan_Shutdown();
    imgui_draw_data_ = nullptr;
}

void VkRenderer::begin_imgui_frame()
{
    if (!ImGui::GetCurrentContext() || !ImGui::GetIO().BackendRendererUserData)
        return;

    ImGui_ImplVulkan_NewFrame();
}

void VkRenderer::set_imgui_draw_data(const ImDrawData* draw_data)
{
    imgui_draw_data_ = draw_data;
}

void VkRenderer::request_frame_capture()
{
    capture_requested_ = true;
}

std::optional<CapturedFrame> VkRenderer::take_captured_frame()
{
    auto frame = std::move(captured_frame_);
    captured_frame_.reset();
    return frame;
}

bool VkRenderer::begin_frame()
{
    vkWaitForFences(ctx_.device(), 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);

    for (auto* handle : grid_handles_)
        handle->state_.restore_cursor();
    upload_dirty_state();

    VkResult result = vkAcquireNextImageKHR(
        ctx_.device(), ctx_.swapchain().swapchain, UINT64_MAX,
        image_available_sem_[current_frame_], VK_NULL_HANDLE, &current_image_);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer, "Vulkan acquire returned VK_ERROR_OUT_OF_DATE_KHR");
        if (!recreate_frame_resources())
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to rebuild Vulkan frame resources after acquire");
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "vkAcquireNextImageKHR failed: %d", (int)result);
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR)
    {
        DRAXUL_LOG_WARN(LogCategory::Renderer, "Vulkan acquire returned VK_SUBOPTIMAL_KHR");
    }

    if (current_image_ < images_in_flight_.size() && images_in_flight_[current_image_] != VK_NULL_HANDLE)
    {
        vkWaitForFences(ctx_.device(), 1, &images_in_flight_[current_image_], VK_TRUE, UINT64_MAX);
    }
    if (current_image_ < images_in_flight_.size())
        images_in_flight_[current_image_] = in_flight_fences_[current_frame_];

    vkResetFences(ctx_.device(), 1, &in_flight_fences_[current_frame_]);
    vkResetCommandBuffer(cmd_buffers_[current_frame_], 0);
    return true;
}

void VkRenderer::record_command_buffer(VkCommandBuffer cmd, uint32_t image_index)
{
    VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &begin_info);

    if (!flush_pending_atlas_uploads(cmd))
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to record pending atlas uploads");

    // Run any registered pre-pass (e.g. GBuffer) before the main render pass.
    if (render_pass_)
    {
        const int vx = viewport3d_x_;
        const int vy = viewport3d_y_;
        const int vw = viewport3d_w_ > 0 ? viewport3d_w_ : pixel_w_;
        const int vh = viewport3d_h_ > 0 ? viewport3d_h_ : pixel_h_;
        VkRenderContext prepass_ctx(cmd, ctx_.physical_device(), ctx_.device(), ctx_.allocator(), VK_NULL_HANDLE,
            current_frame_, MAX_FRAMES_IN_FLIGHT,
            (int)ctx_.swapchain().extent.width, (int)ctx_.swapchain().extent.height,
            vx, vy, vw, vh);
        render_pass_->record_prepass(prepass_ctx);
    }

    VkClearValue clear_values[2] = {};
    clear_values[0].color = { { clear_r_, clear_g_, clear_b_, 1.0f } };
    clear_values[1].depthStencil.depth = 1.0f;
    clear_values[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo rp_begin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp_begin.renderPass = ctx_.render_pass();
    rp_begin.framebuffer = ctx_.swapchain().framebuffers[image_index];
    rp_begin.renderArea.extent = ctx_.swapchain().extent;
    rp_begin.clearValueCount = 2;
    rp_begin.pClearValues = clear_values;

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.width = (float)ctx_.swapchain().extent.width;
    viewport.height = (float)ctx_.swapchain().extent.height;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.extent = ctx_.swapchain().extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    uint32_t instance_offset = 0;
    for (auto* handle : grid_handles_)
    {
        const int bg_instances = handle->state_.bg_instances();
        const int fg_instances = handle->state_.fg_instances();
        const uint32_t handle_span = static_cast<uint32_t>(handle->state_.buffer_size_bytes() / sizeof(GpuCell));
        if (bg_instances <= 0)
        {
            instance_offset += handle_span;
            continue;
        }

        const PaneDescriptor& desc = handle->descriptor_;
        VkRect2D pane_scissor = {};
        if (desc.pixel_size.x > 0 && desc.pixel_size.y > 0)
        {
            pane_scissor.offset.x = std::max(0, desc.pixel_pos.x);
            pane_scissor.offset.y = std::max(0, desc.pixel_pos.y);
            pane_scissor.extent.width = static_cast<uint32_t>(std::max(0,
                std::min(desc.pixel_size.x, pixel_w_ - pane_scissor.offset.x)));
            pane_scissor.extent.height = static_cast<uint32_t>(std::max(0,
                std::min(desc.pixel_size.y, pixel_h_ - pane_scissor.offset.y)));
        }
        else
        {
            pane_scissor.extent = ctx_.swapchain().extent;
        }
        vkCmdSetScissor(cmd, 0, 1, &pane_scissor);

        float push_data[7] = {
            (float)ctx_.swapchain().extent.width,
            (float)ctx_.swapchain().extent.height,
            (float)cell_w_,
            (float)cell_h_,
            handle->scroll_offset_px_,
            (float)desc.pixel_pos.x,
            (float)desc.pixel_pos.y
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.bg_pipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.bg_layout(),
            0, 1, &bg_desc_sets_[current_frame_], 0, nullptr);
        vkCmdPushConstants(cmd, pipeline_.bg_layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_data), push_data);
        vkCmdDraw(cmd, 6, bg_instances, 0, instance_offset);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.fg_pipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.fg_layout(),
            0, 1, &fg_desc_sets_[current_frame_], 0, nullptr);
        vkCmdPushConstants(cmd, pipeline_.fg_layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_data), push_data);
        vkCmdDraw(cmd, 6, fg_instances, 0, instance_offset);

        instance_offset += handle_span;
    }

    if (render_pass_)
    {
        const int vx = viewport3d_x_;
        const int vy = viewport3d_y_;
        const int vw = viewport3d_w_ > 0 ? viewport3d_w_ : pixel_w_;
        const int vh = viewport3d_h_ > 0 ? viewport3d_h_ : pixel_h_;

        VkViewport pass_viewport = {};
        pass_viewport.x = static_cast<float>(vx);
        pass_viewport.y = static_cast<float>(vy);
        pass_viewport.width = static_cast<float>(std::max(0, vw));
        pass_viewport.height = static_cast<float>(std::max(0, vh));
        pass_viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &pass_viewport);

        VkRect2D pass_scissor = {};
        pass_scissor.offset.x = std::max(0, vx);
        pass_scissor.offset.y = std::max(0, vy);
        pass_scissor.extent.width = static_cast<uint32_t>(std::max(0,
            std::min(vw, pixel_w_ - pass_scissor.offset.x)));
        pass_scissor.extent.height = static_cast<uint32_t>(std::max(0,
            std::min(vh, pixel_h_ - pass_scissor.offset.y)));
        vkCmdSetScissor(cmd, 0, 1, &pass_scissor);

        VkRenderContext ctx(cmd, ctx_.physical_device(), ctx_.device(), ctx_.allocator(), ctx_.render_pass(),
            current_frame_, MAX_FRAMES_IN_FLIGHT,
            (int)ctx_.swapchain().extent.width, (int)ctx_.swapchain().extent.height,
            vx, vy, vw, vh);
        render_pass_->record(ctx);
    }

    if (imgui_initialized_ && imgui_draw_data_)
    {
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        ImGui_ImplVulkan_RenderDrawData(const_cast<ImDrawData*>(imgui_draw_data_), cmd);
    }

    vkCmdEndRenderPass(cmd);
    if (capture_requested_)
    {
        transition_image_layout(cmd, ctx_.swapchain().images[image_index],
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy region = {};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { ctx_.swapchain().extent.width, ctx_.swapchain().extent.height, 1 };
        vkCmdCopyImageToBuffer(cmd, ctx_.swapchain().images[image_index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            capture_buffer_, 1, &region);

        transition_image_layout(cmd, ctx_.swapchain().images[image_index],
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_READ_BIT, 0,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    else
    {
        transition_image_layout(cmd, ctx_.swapchain().images[image_index],
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    vkEndCommandBuffer(cmd);
}

bool VkRenderer::flush_pending_atlas_uploads(VkCommandBuffer cmd)
{
    if (pending_atlas_uploads_.empty())
        return true;

    // Per-frame staging buffers eliminate the need to wait for other in-flight
    // frames — each frame slot has its own staging buffer that is safe to reuse
    // once the fence for this frame slot has been waited on (done in begin_frame).
    if (!atlas_.record_uploads(ctx_, cmd, current_frame_, pending_atlas_uploads_))
        return false;

    pending_atlas_uploads_.clear();
    return true;
}

void VkRenderer::end_frame()
{
    for (auto* handle : grid_handles_)
        handle->state_.apply_cursor();
    upload_dirty_state();
    if (capture_requested_ && !ensure_capture_buffer(static_cast<size_t>(ctx_.swapchain().extent.width) * ctx_.swapchain().extent.height * 4))
    {
        capture_requested_ = false;
    }
    record_command_buffer(cmd_buffers_[current_frame_], current_image_);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &image_available_sem_[current_frame_];
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_buffers_[current_frame_];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished_sem_[current_frame_];

    if (vkQueueSubmit(ctx_.graphics_queue(), 1, &submit, in_flight_fences_[current_frame_]) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to submit Vulkan frame");
        return;
    }

    VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished_sem_[current_frame_];
    present.swapchainCount = 1;
    present.pSwapchains = &ctx_.swapchain().swapchain;
    present.pImageIndices = &current_image_;

    VkResult result = vkQueuePresentKHR(ctx_.graphics_queue(), &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebuffer_resized_)
    {
        framebuffer_resized_ = false;
        if (!recreate_frame_resources())
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to rebuild Vulkan frame resources after present");
    }

    if (capture_requested_)
    {
        vkWaitForFences(ctx_.device(), 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
        finish_capture_readback();
    }

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    imgui_draw_data_ = nullptr;
}

} // namespace draxul
