#include "vk_renderer.h"
#include "vk_render_context.h"

#include <algorithm>
#include <backends/imgui_impl_vulkan.h>
#include <cstdio>
#include <cstring>
#include <draxul/log.h>
#include <draxul/pane_descriptor.h>
#include <draxul/perf_timing.h>
#include <draxul/renderer_state.h>
#include <draxul/window.h>
#include <imgui.h>

namespace draxul
{

// ---------------------------------------------------------------------------
// VkGridHandle — per-host grid handle for the Vulkan backend.
// Each handle owns its pane-local cell state, viewport, scroll offset, and
// per-frame GPU buffers / descriptor sets.
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
        retire_gpu_resources();
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
    void set_cursor_visible(bool visible) override
    {
        state_.set_cursor_visible(visible);
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

    bool upload_state(uint32_t frame_index)
    {
        const uint32_t slot = frame_index % VkRenderer::MAX_FRAMES_IN_FLIGHT;
        auto& buffer = buffers_[slot];
        const size_t required_size = state_.buffer_size_bytes();
        const auto resize_result = buffer.ensure_size(renderer_.ctx_.allocator(), required_size);
        if (resize_result == BufferResizeResult::Failed)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to resize Vulkan grid buffer to %zu bytes", required_size);
            return false;
        }
        if (resize_result == BufferResizeResult::Resized && !update_descriptor_sets_for_frame(slot))
            return false;

        auto* mapped = static_cast<std::byte*>(buffer.mapped());
        if (!mapped)
            return false;

        state_.copy_to(mapped);
        buffer.flush_range(renderer_.ctx_.allocator(), 0, required_size);
        state_.clear_dirty();
        return true;
    }

    bool rebuild_descriptor_sets()
    {
        release_descriptor_sets();
        return allocate_descriptor_sets();
    }

    void invalidate_descriptor_sets()
    {
        bg_desc_sets_.fill(VK_NULL_HANDLE);
        fg_desc_sets_.fill(VK_NULL_HANDLE);
    }

    void retire_gpu_resources()
    {
        for (uint32_t i = 0; i < VkRenderer::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            renderer_.retire_grid_slot_resources(i, buffers_[i].release(), renderer_.desc_pool_, bg_desc_sets_[i], fg_desc_sets_[i]);
            bg_desc_sets_[i] = VK_NULL_HANDLE;
            fg_desc_sets_[i] = VK_NULL_HANDLE;
        }
    }

    void release_gpu_resources_immediately()
    {
        release_descriptor_sets();
        bg_desc_sets_.fill(VK_NULL_HANDLE);
        fg_desc_sets_.fill(VK_NULL_HANDLE);
        if (renderer_.ctx_.allocator() != VK_NULL_HANDLE)
        {
            for (auto& buffer : buffers_)
                buffer.shutdown(renderer_.ctx_.allocator());
        }
    }

    VkDescriptorSet bg_desc_set(uint32_t frame_index) const
    {
        return bg_desc_sets_[frame_index % VkRenderer::MAX_FRAMES_IN_FLIGHT];
    }

    VkDescriptorSet fg_desc_set(uint32_t frame_index) const
    {
        return fg_desc_sets_[frame_index % VkRenderer::MAX_FRAMES_IN_FLIGHT];
    }

    RendererState state_;
    PaneDescriptor descriptor_;
    float scroll_offset_px_ = 0.0f;

private:
    bool allocate_descriptor_sets()
    {
        VkDescriptorSetLayout bg_layout = renderer_.pipeline_.bg_desc_layout();
        VkDescriptorSetLayout fg_layout = renderer_.pipeline_.fg_desc_layout();
        if (renderer_.desc_pool_ == VK_NULL_HANDLE || bg_layout == VK_NULL_HANDLE || fg_layout == VK_NULL_HANDLE)
            return false;

        for (uint32_t i = 0; i < VkRenderer::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            alloc_info.descriptorPool = renderer_.desc_pool_;
            alloc_info.descriptorSetCount = 1;
            alloc_info.pSetLayouts = &bg_layout;
            if (vkAllocateDescriptorSets(renderer_.ctx_.device(), &alloc_info, &bg_desc_sets_[i]) != VK_SUCCESS)
                return false;

            alloc_info.pSetLayouts = &fg_layout;
            if (vkAllocateDescriptorSets(renderer_.ctx_.device(), &alloc_info, &fg_desc_sets_[i]) != VK_SUCCESS)
                return false;

            if (!update_descriptor_sets_for_frame(i))
                return false;
        }
        return true;
    }

    void release_descriptor_sets()
    {
        if (renderer_.ctx_.device() == VK_NULL_HANDLE || renderer_.desc_pool_ == VK_NULL_HANDLE)
            return;

        for (auto& set : bg_desc_sets_)
        {
            if (set != VK_NULL_HANDLE)
            {
                vkFreeDescriptorSets(renderer_.ctx_.device(), renderer_.desc_pool_, 1, &set);
                set = VK_NULL_HANDLE;
            }
        }
        for (auto& set : fg_desc_sets_)
        {
            if (set != VK_NULL_HANDLE)
            {
                vkFreeDescriptorSets(renderer_.ctx_.device(), renderer_.desc_pool_, 1, &set);
                set = VK_NULL_HANDLE;
            }
        }
    }

    bool update_descriptor_sets_for_frame(uint32_t frame_index)
    {
        const auto& buffer = buffers_[frame_index];
        if (bg_desc_sets_[frame_index] == VK_NULL_HANDLE || fg_desc_sets_[frame_index] == VK_NULL_HANDLE)
            return false;
        if (buffer.buffer() == VK_NULL_HANDLE)
        {
            DRAXUL_LOG_TRACE(
                LogCategory::Renderer,
                "Deferring Vulkan grid descriptor update for frame %u until the grid buffer exists",
                frame_index);
            return true;
        }

        VkDescriptorBufferInfo buf_info = {};
        buf_info.buffer = buffer.buffer();
        buf_info.offset = 0;
        buf_info.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet writes[3] = {};
        writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        writes[0].dstSet = bg_desc_sets_[frame_index];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &buf_info;

        writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        writes[1].dstSet = fg_desc_sets_[frame_index];
        writes[1].dstBinding = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &buf_info;

        VkDescriptorImageInfo img_info = {};
        img_info.sampler = renderer_.atlas_.sampler();
        img_info.imageView = renderer_.atlas_.image_view();
        img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        writes[2].dstSet = fg_desc_sets_[frame_index];
        writes[2].dstBinding = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &img_info;

        vkUpdateDescriptorSets(renderer_.ctx_.device(), 3, writes, 0, nullptr);
        return true;
    }

    VkRenderer& renderer_;
    int padding_ = 4;
    std::array<VkGridBuffer, VkRenderer::MAX_FRAMES_IN_FLIGHT> buffers_;
    std::array<VkDescriptorSet, VkRenderer::MAX_FRAMES_IN_FLIGHT> bg_desc_sets_{};
    std::array<VkDescriptorSet, VkRenderer::MAX_FRAMES_IN_FLIGHT> fg_desc_sets_{};
};

class VkRenderer::FrameContext final : public IFrameContext
{
public:
    explicit FrameContext(VkRenderer& renderer)
        : renderer_(renderer)
    {
    }

    void draw_grid_handle(IGridHandle& handle) override
    {
        renderer_.draw_grid_handle_now(handle);
    }

    void record_render_pass(IRenderPass& pass, const RenderViewport& viewport) override
    {
        renderer_.record_render_pass_now(pass, viewport);
    }

    void render_imgui(const ImDrawData* draw_data, ImGuiContext* context) override
    {
        renderer_.render_imgui_now(draw_data, context);
    }

    void flush_submit_chunk() override
    {
        renderer_.flush_submit_chunk(false);
    }

private:
    VkRenderer& renderer_;
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
    , frame_context_(std::make_unique<FrameContext>(*this))
{
    PERF_MEASURE();
}

void VkRenderer::retire_grid_slot_resources(uint32_t frame_index, VkGridBuffer::BufferState buffer, VkDescriptorPool descriptor_pool,
    VkDescriptorSet bg_desc_set, VkDescriptorSet fg_desc_set)
{
    if (buffer.buffer == VK_NULL_HANDLE && bg_desc_set == VK_NULL_HANDLE && fg_desc_set == VK_NULL_HANDLE)
        return;

    RetiredGridSlotResources resource;
    resource.buffer = buffer;
    resource.descriptor_pool = descriptor_pool;
    resource.bg_desc_set = bg_desc_set;
    resource.fg_desc_set = fg_desc_set;
    retired_grid_slot_resources_[frame_index % MAX_FRAMES_IN_FLIGHT].push_back(std::move(resource));
}

void VkRenderer::reclaim_retired_grid_slot_resources(uint32_t frame_index)
{
    if (ctx_.device() == VK_NULL_HANDLE || ctx_.allocator() == VK_NULL_HANDLE)
        return;

    auto& retired = retired_grid_slot_resources_[frame_index % MAX_FRAMES_IN_FLIGHT];
    for (auto& resource : retired)
    {
        VkGridBuffer::destroy_buffer_state(ctx_.allocator(), resource.buffer);
        if (resource.descriptor_pool != VK_NULL_HANDLE)
        {
            if (resource.bg_desc_set != VK_NULL_HANDLE)
                vkFreeDescriptorSets(ctx_.device(), resource.descriptor_pool, 1, &resource.bg_desc_set);
            if (resource.fg_desc_set != VK_NULL_HANDLE)
                vkFreeDescriptorSets(ctx_.device(), resource.descriptor_pool, 1, &resource.fg_desc_set);
        }
    }
    retired.clear();
}

void VkRenderer::reclaim_all_retired_grid_slot_resources()
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        reclaim_retired_grid_slot_resources(i);
}

bool VkRenderer::ensure_capture_buffer(size_t required_size)
{
    PERF_MEASURE();
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
    PERF_MEASURE();
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
    PERF_MEASURE();
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
    PERF_MEASURE();
    if (!ctx_.initialize(static_cast<SDL_Window*>(window.native_handle())))
        return false;

    auto [w, h] = window.size_pixels();
    pixel_w_ = w;
    pixel_h_ = h;

    if (!atlas_.initialize(ctx_, atlas_size_))
        return false;
    if (!pipeline_.initialize(ctx_.device(), ctx_.render_pass(), "shaders"))
        return false;
    if (!create_command_buffers())
        return false;
    if (!create_sync_objects())
        return false;
    if (!create_descriptor_pool(desc_pool_))
        return false;
    images_in_flight_.assign(ctx_.swapchain().images.size(), VK_NULL_HANDLE);
    return true;
}

void VkRenderer::shutdown()
{
    PERF_MEASURE();
    if (ctx_.device())
        vkDeviceWaitIdle(ctx_.device());

    for (auto* handle : grid_handles_)
        handle->release_gpu_resources_immediately();
    reclaim_all_retired_grid_slot_resources();

    for (auto& sem : image_available_sem_)
        vkDestroySemaphore(ctx_.device(), sem, nullptr);
    for (auto& sem : render_finished_sem_)
        vkDestroySemaphore(ctx_.device(), sem, nullptr);
    for (auto& fence : in_flight_fences_)
        vkDestroyFence(ctx_.device(), fence, nullptr);
    if (cmd_pool_)
        vkDestroyCommandPool(ctx_.device(), cmd_pool_, nullptr);
    if (desc_pool_)
    {
        vkDestroyDescriptorPool(ctx_.device(), desc_pool_, nullptr);
        desc_pool_ = VK_NULL_HANDLE;
    }
    if (imgui_desc_pool_)
    {
        vkDestroyDescriptorPool(ctx_.device(), imgui_desc_pool_, nullptr);
        imgui_desc_pool_ = VK_NULL_HANDLE;
    }
    destroy_capture_buffer();
    pipeline_.shutdown(ctx_.device());
    atlas_.shutdown(ctx_);
    ctx_.shutdown();
}

bool VkRenderer::create_sync_objects()
{
    PERF_MEASURE();
    image_available_sem_.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_sem_.resize(MAX_FRAMES_IN_FLIGHT);
    in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo sem_ci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_ci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(ctx_.device(), &sem_ci, nullptr, &image_available_sem_[i]) != VK_SUCCESS)
        {
            for (int j = 0; j < i; j++)
            {
                vkDestroySemaphore(ctx_.device(), image_available_sem_[j], nullptr);
                vkDestroySemaphore(ctx_.device(), render_finished_sem_[j], nullptr);
                vkDestroyFence(ctx_.device(), in_flight_fences_[j], nullptr);
                image_available_sem_[j] = VK_NULL_HANDLE;
                render_finished_sem_[j] = VK_NULL_HANDLE;
                in_flight_fences_[j] = VK_NULL_HANDLE;
            }
            return false;
        }
        if (vkCreateSemaphore(ctx_.device(), &sem_ci, nullptr, &render_finished_sem_[i]) != VK_SUCCESS)
        {
            vkDestroySemaphore(ctx_.device(), image_available_sem_[i], nullptr);
            image_available_sem_[i] = VK_NULL_HANDLE;
            for (int j = 0; j < i; j++)
            {
                vkDestroySemaphore(ctx_.device(), image_available_sem_[j], nullptr);
                vkDestroySemaphore(ctx_.device(), render_finished_sem_[j], nullptr);
                vkDestroyFence(ctx_.device(), in_flight_fences_[j], nullptr);
                image_available_sem_[j] = VK_NULL_HANDLE;
                render_finished_sem_[j] = VK_NULL_HANDLE;
                in_flight_fences_[j] = VK_NULL_HANDLE;
            }
            return false;
        }
        if (vkCreateFence(ctx_.device(), &fence_ci, nullptr, &in_flight_fences_[i]) != VK_SUCCESS)
        {
            vkDestroySemaphore(ctx_.device(), image_available_sem_[i], nullptr);
            vkDestroySemaphore(ctx_.device(), render_finished_sem_[i], nullptr);
            image_available_sem_[i] = VK_NULL_HANDLE;
            render_finished_sem_[i] = VK_NULL_HANDLE;
            for (int j = 0; j < i; j++)
            {
                vkDestroySemaphore(ctx_.device(), image_available_sem_[j], nullptr);
                vkDestroySemaphore(ctx_.device(), render_finished_sem_[j], nullptr);
                vkDestroyFence(ctx_.device(), in_flight_fences_[j], nullptr);
                image_available_sem_[j] = VK_NULL_HANDLE;
                render_finished_sem_[j] = VK_NULL_HANDLE;
                in_flight_fences_[j] = VK_NULL_HANDLE;
            }
            return false;
        }
    }
    return true;
}

bool VkRenderer::create_command_buffers()
{
    PERF_MEASURE();
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

bool VkRenderer::create_descriptor_pool(VkDescriptorPool& pool)
{
    PERF_MEASURE();
    constexpr uint32_t kMaxGridHandles = 128;
    constexpr uint32_t kDescriptorSetsPerHandle = MAX_FRAMES_IN_FLIGHT * 2;
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxGridHandles * kDescriptorSetsPerHandle },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxGridHandles * MAX_FRAMES_IN_FLIGHT },
    };

    VkDescriptorPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pool_ci.maxSets = kMaxGridHandles * kDescriptorSetsPerHandle;
    pool_ci.poolSizeCount = 2;
    pool_ci.pPoolSizes = pool_sizes;
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    return vkCreateDescriptorPool(ctx_.device(), &pool_ci, nullptr, &pool) == VK_SUCCESS;
}

bool VkRenderer::recreate_frame_resources()
{
    PERF_MEASURE();
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
    if (!create_descriptor_pool(pending_desc_pool))
    {
        if (pending_desc_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(ctx_.device(), pending_desc_pool, nullptr);
        pending_pipeline.shutdown(ctx_.device());
        ctx_.destroy_pending_swapchain_resources(pending_swapchain);
        return false;
    }

    VkDescriptorPool old_desc_pool = desc_pool_;
    for (auto* handle : grid_handles_)
        handle->invalidate_descriptor_sets();

    ctx_.commit_swapchain_resources(std::move(pending_swapchain));
    reclaim_all_retired_grid_slot_resources();

    pipeline_.swap(pending_pipeline);
    pending_pipeline.shutdown(ctx_.device());

    desc_pool_ = pending_desc_pool;
    for (auto* handle : grid_handles_)
    {
        if (!handle->rebuild_descriptor_sets())
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to rebuild Vulkan grid descriptor sets");
            if (old_desc_pool != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(ctx_.device(), old_desc_pool, nullptr);
            return false;
        }
    }
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
void VkRenderer::set_atlas_texture(const uint8_t* data, int w, int h)
{
    PERF_MEASURE();
    queue_full_atlas_upload(pending_atlas_uploads_, data, w, h);
}

void VkRenderer::update_atlas_region(int x, int y, int w, int h, const uint8_t* data)
{
    PERF_MEASURE();
    queue_atlas_region_upload(pending_atlas_uploads_, x, y, w, h, data);
}

void VkRenderer::resize(int pixel_w, int pixel_h)
{
    PERF_MEASURE();
    pixel_w_ = pixel_w;
    pixel_h_ = pixel_h;
    framebuffer_resized_ = true;
}

std::pair<int, int> VkRenderer::cell_size_pixels() const
{
    PERF_MEASURE();
    return { cell_w_, cell_h_ };
}

void VkRenderer::set_cell_size(int w, int h)
{
    PERF_MEASURE();
    cell_w_ = w;
    cell_h_ = h;
    for (auto* handle : grid_handles_)
        handle->state_.set_cell_size(w, h);
}

void VkRenderer::set_ascender(int a)
{
    PERF_MEASURE();
    ascender_ = a;
    for (auto* handle : grid_handles_)
        handle->state_.set_ascender(a);
}

std::unique_ptr<IGridHandle> VkRenderer::create_grid_handle()
{
    PERF_MEASURE();
    auto handle = std::make_unique<VkGridHandle>(*this, padding_);
    handle->state_.set_cell_size(cell_w_, cell_h_);
    handle->state_.set_ascender(ascender_);
    if (!handle->rebuild_descriptor_sets())
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to allocate Vulkan descriptor sets for grid handle");
        return {};
    }
    return handle;
}

void VkRenderer::set_default_background(Color bg)
{
    PERF_MEASURE();
    clear_r_ = bg.r;
    clear_g_ = bg.g;
    clear_b_ = bg.b;
}

bool VkRenderer::create_imgui_descriptor_pool()
{
    PERF_MEASURE();
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
    PERF_MEASURE();
    const bool ok = ImGui_ImplVulkan_CreateFontsTexture();
    if (ok)
        imgui_font_texture_rebuild_pending_ = false;
    return ok;
}

void VkRenderer::rebuild_imgui_font_texture()
{
    PERF_MEASURE();
    if (!ImGui::GetCurrentContext() || !ImGui::GetIO().BackendRendererUserData)
        return;

    imgui_font_texture_rebuild_pending_ = true;
    if (frame_active_)
        return;

    if (!create_imgui_font_texture())
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to rebuild ImGui Vulkan font texture");
}

bool VkRenderer::initialize_imgui_backend()
{
    PERF_MEASURE();
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
    imgui_font_texture_rebuild_pending_ = false;
    return true;
}

void VkRenderer::shutdown_imgui_backend()
{
    PERF_MEASURE();
    // Shuts down the backend for the current ImGui context only.
    if (!ImGui::GetCurrentContext() || !ImGui::GetIO().BackendRendererUserData)
        return;

    vkDeviceWaitIdle(ctx_.device());
    ImGui_ImplVulkan_Shutdown();
}

void VkRenderer::begin_imgui_frame()
{
    PERF_MEASURE();
    if (!ImGui::GetCurrentContext() || !ImGui::GetIO().BackendRendererUserData)
        return;

    if (imgui_font_texture_rebuild_pending_ && !create_imgui_font_texture())
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to apply pending ImGui Vulkan font texture rebuild");

    ImGui_ImplVulkan_NewFrame();
}

void VkRenderer::request_frame_capture()
{
    PERF_MEASURE();
    capture_requested_ = true;
}

std::optional<CapturedFrame> VkRenderer::take_captured_frame()
{
    PERF_MEASURE();
    auto frame = std::move(captured_frame_);
    captured_frame_.reset();
    return frame;
}

IFrameContext* VkRenderer::begin_frame()
{
    bool success = false;
    {
        PERF_MEASURE();
        if (in_flight_fences_[current_frame_] != VK_NULL_HANDLE)
            vkWaitForFences(ctx_.device(), 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
        reclaim_retired_grid_slot_resources(current_frame_);

        for (auto* handle : grid_handles_)
            handle->state_.restore_cursor();

        bool had_pending_atlas_uploads = false;
        VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

        VkResult result = vkAcquireNextImageKHR(
            ctx_.device(), ctx_.swapchain().swapchain, UINT64_MAX,
            image_available_sem_[current_frame_], VK_NULL_HANDLE, &current_image_);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            DRAXUL_LOG_WARN(LogCategory::Renderer, "Vulkan acquire returned VK_ERROR_OUT_OF_DATE_KHR");
            if (!recreate_frame_resources())
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to rebuild Vulkan frame resources after acquire");
            success = false;
            goto finish_begin_frame;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "vkAcquireNextImageKHR failed: %d", (int)result);
            goto finish_begin_frame;
        }
        if (result == VK_SUBOPTIMAL_KHR)
        {
            DRAXUL_LOG_WARN(LogCategory::Renderer, "Vulkan acquire returned VK_SUBOPTIMAL_KHR");
        }

        if (current_image_ < images_in_flight_.size() && images_in_flight_[current_image_] != VK_NULL_HANDLE)
        {
            vkWaitForFences(ctx_.device(), 1, &images_in_flight_[current_image_], VK_TRUE, UINT64_MAX);
        }
        current_chunk_index_ = 0;
        vkResetCommandBuffer(cmd_buffers_[current_frame_], 0);
        active_cmd_buffer_ = cmd_buffers_[current_frame_];
        frame_active_ = false;
        main_render_pass_active_ = false;
        main_render_pass_started_ = false;
        had_pending_atlas_uploads = !pending_atlas_uploads_.empty();
        if (vkBeginCommandBuffer(active_cmd_buffer_, &begin_info) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to begin Vulkan command buffer");
            active_cmd_buffer_ = VK_NULL_HANDLE;
            goto finish_begin_frame;
        }
        if (!flush_pending_atlas_uploads(active_cmd_buffer_))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to record pending atlas uploads");
            vkEndCommandBuffer(active_cmd_buffer_);
            active_cmd_buffer_ = VK_NULL_HANDLE;
            goto finish_begin_frame;
        }
        chunk_has_work_ = had_pending_atlas_uploads;
        frame_active_ = true;
        success = true;
    finish_begin_frame:;
    }
    if (!success)
        runtime_perf_collector().cancel_frame();
    return success ? frame_context_.get() : nullptr;
}

bool VkRenderer::start_new_chunk_command_buffer()
{
    if (!frame_active_)
        return false;

    ++current_chunk_index_;
    auto& extra = extra_cmd_buffers_[current_frame_];
    const size_t extra_index = static_cast<size_t>(current_chunk_index_ - 1);
    if (extra_index >= extra.size())
    {
        VkCommandBufferAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        alloc_info.commandPool = cmd_pool_;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;
        extra.emplace_back(VK_NULL_HANDLE);
        if (vkAllocateCommandBuffers(ctx_.device(), &alloc_info, &extra.back()) != VK_SUCCESS)
        {
            extra.pop_back();
            return false;
        }
    }

    active_cmd_buffer_ = extra[extra_index];
    vkResetCommandBuffer(active_cmd_buffer_, 0);

    VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    if (vkBeginCommandBuffer(active_cmd_buffer_, &begin_info) != VK_SUCCESS)
        return false;

    main_render_pass_active_ = false;
    chunk_has_work_ = false;
    return true;
}

bool VkRenderer::begin_main_render_pass()
{
    if (!frame_active_ || active_cmd_buffer_ == VK_NULL_HANDLE)
        return false;
    if (main_render_pass_active_)
        return true;

    VkClearValue clear_values[2] = {};
    clear_values[0].color = { { clear_r_, clear_g_, clear_b_, 1.0f } };
    clear_values[1].depthStencil.depth = 1.0f;
    clear_values[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo rp_begin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp_begin.renderPass = main_render_pass_started_ ? ctx_.load_render_pass() : ctx_.render_pass();
    rp_begin.framebuffer = ctx_.swapchain().framebuffers[current_image_];
    rp_begin.renderArea.extent = ctx_.swapchain().extent;
    rp_begin.clearValueCount = 2;
    rp_begin.pClearValues = clear_values;

    vkCmdBeginRenderPass(active_cmd_buffer_, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.width = (float)ctx_.swapchain().extent.width;
    viewport.height = (float)ctx_.swapchain().extent.height;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(active_cmd_buffer_, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.extent = ctx_.swapchain().extent;
    vkCmdSetScissor(active_cmd_buffer_, 0, 1, &scissor);
    main_render_pass_active_ = true;
    main_render_pass_started_ = true;
    return true;
}

void VkRenderer::end_main_render_pass()
{
    if (!main_render_pass_active_ || active_cmd_buffer_ == VK_NULL_HANDLE)
        return;
    vkCmdEndRenderPass(active_cmd_buffer_);
    main_render_pass_active_ = false;
}

bool VkRenderer::draw_grid_handle_now(IGridHandle& handle)
{
    auto* vk_handle = dynamic_cast<VkGridHandle*>(&handle);
    if (!vk_handle || !frame_active_)
        return false;

    const PaneDescriptor& desc = vk_handle->descriptor_;
    // Skip drawing entirely when the viewport has zero area (e.g. zoomed-out panes).
    if (desc.pixel_size.x <= 0 || desc.pixel_size.y <= 0)
        return true;

    vk_handle->state_.apply_cursor();
    if (!vk_handle->upload_state(current_frame_))
    {
        runtime_perf_collector().cancel_frame();
        return false;
    }

    if (!begin_main_render_pass())
        return false;

    const int bg_instances = vk_handle->state_.bg_instances();
    const int fg_instances = vk_handle->state_.fg_instances();
    if (bg_instances <= 0)
        return true;

    VkViewport viewport = {};
    viewport.width = static_cast<float>(ctx_.swapchain().extent.width);
    viewport.height = static_cast<float>(ctx_.swapchain().extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(active_cmd_buffer_, 0, 1, &viewport);

    VkRect2D pane_scissor = {};
    pane_scissor.offset.x = std::max(0, desc.pixel_pos.x);
    pane_scissor.offset.y = std::max(0, desc.pixel_pos.y);
    pane_scissor.extent.width = static_cast<uint32_t>(std::max(0,
        std::min(desc.pixel_size.x, pixel_w_ - pane_scissor.offset.x)));
    pane_scissor.extent.height = static_cast<uint32_t>(std::max(0,
        std::min(desc.pixel_size.y, pixel_h_ - pane_scissor.offset.y)));
    vkCmdSetScissor(active_cmd_buffer_, 0, 1, &pane_scissor);

    float push_data[7] = {
        static_cast<float>(ctx_.swapchain().extent.width),
        static_cast<float>(ctx_.swapchain().extent.height),
        static_cast<float>(cell_w_),
        static_cast<float>(cell_h_),
        vk_handle->scroll_offset_px_,
        static_cast<float>(desc.pixel_pos.x),
        static_cast<float>(desc.pixel_pos.y)
    };

    const VkDescriptorSet bg_desc_set = vk_handle->bg_desc_set(current_frame_);
    const VkDescriptorSet fg_desc_set = vk_handle->fg_desc_set(current_frame_);

    vkCmdBindPipeline(active_cmd_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.bg_pipeline());
    vkCmdBindDescriptorSets(active_cmd_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.bg_layout(),
        0, 1, &bg_desc_set, 0, nullptr);
    vkCmdPushConstants(active_cmd_buffer_, pipeline_.bg_layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_data), push_data);
    vkCmdDraw(active_cmd_buffer_, 6, bg_instances, 0, 0);

    vkCmdBindPipeline(active_cmd_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.fg_pipeline());
    vkCmdBindDescriptorSets(active_cmd_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.fg_layout(),
        0, 1, &fg_desc_set, 0, nullptr);
    vkCmdPushConstants(active_cmd_buffer_, pipeline_.fg_layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_data), push_data);
    vkCmdDraw(active_cmd_buffer_, 6, fg_instances, 0, 0);
    chunk_has_work_ = true;
    return true;
}

bool VkRenderer::record_render_pass_now(IRenderPass& pass, const RenderViewport& viewport)
{
    if (!frame_active_ || active_cmd_buffer_ == VK_NULL_HANDLE)
        return false;

    if (main_render_pass_active_)
        end_main_render_pass();

    // Vulkan prepasses such as NanoVG can render straight into the swapchain
    // image before the regular renderer has touched it for this frame. Prime
    // the main render pass once up front so the image is cleared and left in
    // COLOR_ATTACHMENT_OPTIMAL; subsequent main-pass resumes will then load the
    // prepass content instead of clearing over it.
    if (!main_render_pass_started_)
    {
        if (!begin_main_render_pass())
            return false;
        end_main_render_pass();
    }

    const int vx = viewport.x;
    const int vy = viewport.y;
    const int vw = viewport.width > 0 ? viewport.width : pixel_w_;
    const int vh = viewport.height > 0 ? viewport.height : pixel_h_;

    VkRenderContext prepass_ctx(active_cmd_buffer_, ctx_.physical_device(), ctx_.device(), ctx_.allocator(), VK_NULL_HANDLE,
        current_frame_, MAX_FRAMES_IN_FLIGHT,
        static_cast<int>(ctx_.swapchain().extent.width), static_cast<int>(ctx_.swapchain().extent.height),
        vx, vy, vw, vh,
        ctx_.swapchain().images[current_image_], ctx_.swapchain().image_views[current_image_],
        ctx_.swapchain().format, ctx_.graphics_queue(), ctx_.graphics_queue_family());
    pass.record_prepass(prepass_ctx);

    // Prepasses such as NanoVG render directly into the swapchain image using
    // their own render pass. Make those color writes visible before we resume
    // the renderer's main pass and load from the same attachment.
    VkImageMemoryBarrier prepass_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    prepass_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    prepass_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    prepass_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    prepass_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    prepass_barrier.image = ctx_.swapchain().images[current_image_];
    prepass_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    prepass_barrier.subresourceRange.baseMipLevel = 0;
    prepass_barrier.subresourceRange.levelCount = 1;
    prepass_barrier.subresourceRange.baseArrayLayer = 0;
    prepass_barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(active_cmd_buffer_,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &prepass_barrier);

    if (!begin_main_render_pass())
        return false;

    VkViewport pass_viewport = {};
    pass_viewport.x = static_cast<float>(vx);
    pass_viewport.y = static_cast<float>(vy);
    pass_viewport.width = static_cast<float>(std::max(0, vw));
    pass_viewport.height = static_cast<float>(std::max(0, vh));
    pass_viewport.maxDepth = 1.0f;
    vkCmdSetViewport(active_cmd_buffer_, 0, 1, &pass_viewport);

    VkRect2D pass_scissor = {};
    pass_scissor.offset.x = std::max(0, vx);
    pass_scissor.offset.y = std::max(0, vy);
    pass_scissor.extent.width = static_cast<uint32_t>(std::max(0,
        std::min(vw, pixel_w_ - pass_scissor.offset.x)));
    pass_scissor.extent.height = static_cast<uint32_t>(std::max(0,
        std::min(vh, pixel_h_ - pass_scissor.offset.y)));
    vkCmdSetScissor(active_cmd_buffer_, 0, 1, &pass_scissor);

    VkRenderContext ctx(active_cmd_buffer_, ctx_.physical_device(), ctx_.device(), ctx_.allocator(), ctx_.render_pass(),
        current_frame_, MAX_FRAMES_IN_FLIGHT,
        static_cast<int>(ctx_.swapchain().extent.width), static_cast<int>(ctx_.swapchain().extent.height),
        vx, vy, vw, vh);
    pass.record(ctx);
    chunk_has_work_ = true;
    return true;
}

bool VkRenderer::render_imgui_now(const ImDrawData* draw_data, ImGuiContext* context)
{
    if (!draw_data || !context || !frame_active_)
        return false;
    if (!begin_main_render_pass())
        return false;

    VkViewport viewport = {};
    viewport.width = static_cast<float>(ctx_.swapchain().extent.width);
    viewport.height = static_cast<float>(ctx_.swapchain().extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(active_cmd_buffer_, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.extent = ctx_.swapchain().extent;
    vkCmdSetScissor(active_cmd_buffer_, 0, 1, &scissor);

    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context);
    ImGui_ImplVulkan_RenderDrawData(const_cast<ImDrawData*>(draw_data), active_cmd_buffer_);
    ImGui::SetCurrentContext(prev_ctx);
    chunk_has_work_ = true;
    return true;
}

bool VkRenderer::flush_pending_atlas_uploads(VkCommandBuffer cmd)
{
    PERF_MEASURE();
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

bool VkRenderer::flush_submit_chunk(bool final_chunk)
{
    if (!frame_active_ || active_cmd_buffer_ == VK_NULL_HANDLE)
        return false;

    if (main_render_pass_active_)
        end_main_render_pass();

    if (!chunk_has_work_ && !final_chunk)
        return true;

    if (!chunk_has_work_ && final_chunk && !main_render_pass_started_)
    {
        if (!begin_main_render_pass())
            return false;
        end_main_render_pass();
        chunk_has_work_ = true;
    }

    if (final_chunk)
    {
        if (capture_requested_)
        {
            transition_image_layout(active_cmd_buffer_, ctx_.swapchain().images[current_image_],
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkBufferImageCopy region = {};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = { ctx_.swapchain().extent.width, ctx_.swapchain().extent.height, 1 };
            vkCmdCopyImageToBuffer(active_cmd_buffer_, ctx_.swapchain().images[current_image_], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                capture_buffer_, 1, &region);

            transition_image_layout(active_cmd_buffer_, ctx_.swapchain().images[current_image_],
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_TRANSFER_READ_BIT, 0,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
            chunk_has_work_ = true;
        }
        else
        {
            const VkImageLayout from_layout = main_render_pass_started_
                ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED;
            transition_image_layout(active_cmd_buffer_, ctx_.swapchain().images[current_image_],
                from_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                main_render_pass_started_ ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0, 0,
                main_render_pass_started_ ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        }

        if (vkEndCommandBuffer(active_cmd_buffer_) != VK_SUCCESS)
            return false;

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.waitSemaphoreCount = current_chunk_index_ == 0 ? 1u : 0u;
        submit.pWaitSemaphores = current_chunk_index_ == 0 ? &image_available_sem_[current_frame_] : nullptr;
        submit.pWaitDstStageMask = current_chunk_index_ == 0 ? &wait_stage : nullptr;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &active_cmd_buffer_;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_finished_sem_[current_frame_];

        vkResetFences(ctx_.device(), 1, &in_flight_fences_[current_frame_]);
        if (current_image_ < images_in_flight_.size())
            images_in_flight_[current_image_] = in_flight_fences_[current_frame_];

        if (vkQueueSubmit(ctx_.graphics_queue(), 1, &submit, in_flight_fences_[current_frame_]) != VK_SUCCESS)
            return false;

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
        active_cmd_buffer_ = VK_NULL_HANDLE;
        frame_active_ = false;
        main_render_pass_active_ = false;
        main_render_pass_started_ = false;
        chunk_has_work_ = false;
        return true;
    }

    if (vkEndCommandBuffer(active_cmd_buffer_) != VK_SUCCESS)
        return false;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = current_chunk_index_ == 0 ? 1u : 0u;
    submit.pWaitSemaphores = current_chunk_index_ == 0 ? &image_available_sem_[current_frame_] : nullptr;
    submit.pWaitDstStageMask = current_chunk_index_ == 0 ? &wait_stage : nullptr;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &active_cmd_buffer_;

    if (vkQueueSubmit(ctx_.graphics_queue(), 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
        return false;

    return start_new_chunk_command_buffer();
}

void VkRenderer::end_frame()
{
    {
        PERF_MEASURE();
        if (!frame_active_ || active_cmd_buffer_ == VK_NULL_HANDLE)
            return;
        if (capture_requested_ && !ensure_capture_buffer(static_cast<size_t>(ctx_.swapchain().extent.width) * ctx_.swapchain().extent.height * 4))
        {
            capture_requested_ = false;
        }
        if (!flush_submit_chunk(true))
        {
            runtime_perf_collector().cancel_frame();
            active_cmd_buffer_ = VK_NULL_HANDLE;
            frame_active_ = false;
            main_render_pass_active_ = false;
            main_render_pass_started_ = false;
            chunk_has_work_ = false;
        }
    }
}

} // namespace draxul
