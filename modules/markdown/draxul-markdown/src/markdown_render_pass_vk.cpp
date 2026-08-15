#include <draxul/markdown/markdown_render_pass.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <draxul/log.h>
#include <draxul/runtime_path.h>
#include <draxul/vulkan/vk_render_context.h>
#include <filesystem>
#include <map>
#include <utility>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <draxul/vulkan/vk_resource_helpers.h>

namespace draxul::markdown
{
namespace
{

constexpr VkDeviceSize kMinBufferSize = 16;
constexpr uint32_t kDescriptorPoolMaxSets = 4096;
constexpr uint32_t kAtlasChannels = 4;

struct Buffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

struct Image
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct PushConstants
{
    float screen_w = 0.0f;
    float screen_h = 0.0f;
    float viewport_x = 0.0f;
    float viewport_y = 0.0f;
};

struct UploadCopy
{
    const MarkdownAtlasUpload* upload = nullptr;
    VkDeviceSize staging_offset = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    size_t bytes = 0;
};

template <typename T>
bool instance_range_valid(const std::vector<T>& instances, uint32_t first, uint32_t count)
{
    return first <= instances.size() && count <= instances.size() - first;
}

VkShaderModule load_shader(VkDevice device, const std::filesystem::path& path)
{
    std::string error;
    const std::string debug_name = "markdown.shader." + path.filename().string();
    const VkShaderModule shader = vkresources::load_shader_module(device, path, debug_name, error);
    if (shader == VK_NULL_HANDLE)
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: %s", error.c_str());
    return shader;
}

void destroy_buffer(VmaAllocator allocator, Buffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

void destroy_image(VkDevice device, VmaAllocator allocator, Image& image)
{
    if (image.view != VK_NULL_HANDLE)
        vkDestroyImageView(device, image.view, nullptr);
    if (image.image != VK_NULL_HANDLE)
        vmaDestroyImage(allocator, image.image, image.allocation);
    image = {};
}

bool ensure_mapped_buffer(
    VkDevice device,
    VmaAllocator allocator,
    Buffer& buffer,
    VkDeviceSize required_size,
    VkBufferUsageFlags usage,
    vkresources::LifetimeScope lifetime,
    const char* label)
{
    if (required_size == 0)
        return true;

    const VkDeviceSize allocation_size = std::max(required_size, kMinBufferSize);
    if (buffer.buffer != VK_NULL_HANDLE && buffer.size >= allocation_size)
        return true;

    // Markdown buffers persist with their owning pass/frame. The caller still
    // owns retirement and explicitly waits before atlas replacement.
    vkresources::ScopedBuffer replacement;
    std::string error;
    if (!vkresources::create_buffer(device, allocator,
            vkresources::BufferRequest(allocation_size, usage,
                vkresources::MemoryPolicy::HostSequentialWrite,
                std::string("markdown.") + label,
                lifetime),
            replacement, error))
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create %s buffer: %s", label, error.c_str());
        return false;
    }

    const vkresources::BufferResource created = replacement.release();
    destroy_buffer(allocator, buffer);
    buffer.buffer = created.buffer;
    buffer.allocation = created.allocation;
    buffer.mapped = created.mapped;
    buffer.size = created.size;
    return true;
}

bool upload_to_mapped_buffer(VmaAllocator allocator, Buffer& buffer, const void* data, size_t bytes)
{
    if (bytes == 0)
        return true;
    if (buffer.mapped == nullptr)
        return false;

    std::memcpy(buffer.mapped, data, bytes);
    vmaFlushAllocation(allocator, buffer.allocation, 0, bytes);
    return true;
}

void transition_image(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkAccessFlags src_access,
    VkAccessFlags dst_access,
    VkPipelineStageFlags src_stage,
    VkPipelineStageFlags dst_stage)
{
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkresources::transition_image(cmd, vkresources::ImageTransition{
        image, old_layout, new_layout, src_access, dst_access, src_stage, dst_stage, range });
}

int clamped_extent(int origin, int requested, int limit)
{
    if (requested <= 0 || limit <= 0 || origin >= limit)
        return 0;
    return std::max(0, std::min(requested, limit - std::max(0, origin)));
}

} // namespace

struct MarkdownRenderPass::State
{
    struct AtlasTexture
    {
        Image image;
        uint32_t generation = 0;
        uint64_t upload_revision = 0;
    };

    struct FrameResources
    {
        Buffer rect_buffer;
        Buffer glyph_buffer;
        Buffer atlas_staging;
        VkDescriptorSet rect_descriptor_set = VK_NULL_HANDLE;
        std::map<uint32_t, VkDescriptorSet> glyph_descriptor_sets;
        uint64_t uploaded_draw_revision = 0;
    };

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;

    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout rect_descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout glyph_descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout rect_pipeline_layout = VK_NULL_HANDLE;
    VkPipelineLayout glyph_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline rect_pipeline = VK_NULL_HANDLE;
    VkPipeline glyph_pipeline = VK_NULL_HANDLE;

    std::vector<FrameResources> frames;
    uint32_t buffered_frame_count = 0;
    std::map<uint32_t, AtlasTexture> atlases;

    void shutdown()
    {
        if (device == VK_NULL_HANDLE)
            return;

        vkDeviceWaitIdle(device);
        destroy_pipelines();
        if (rect_pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, rect_pipeline_layout, nullptr);
        if (glyph_pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, glyph_pipeline_layout, nullptr);
        if (descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (rect_descriptor_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, rect_descriptor_layout, nullptr);
        if (glyph_descriptor_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, glyph_descriptor_layout, nullptr);
        if (sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, sampler, nullptr);

        for (auto& frame : frames)
        {
            destroy_buffer(allocator, frame.rect_buffer);
            destroy_buffer(allocator, frame.glyph_buffer);
            destroy_buffer(allocator, frame.atlas_staging);
        }
        for (auto& [atlas_id, atlas] : atlases)
        {
            (void)atlas_id;
            destroy_image(device, allocator, atlas.image);
        }
        *this = {};
    }

    void destroy_pipelines()
    {
        if (rect_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, rect_pipeline, nullptr);
        if (glyph_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, glyph_pipeline, nullptr);
        rect_pipeline = VK_NULL_HANDLE;
        glyph_pipeline = VK_NULL_HANDLE;
    }

    bool ensure_device(const VkRenderContext& ctx)
    {
        if (device == ctx.device() && allocator == ctx.allocator())
            return true;
        shutdown();
        device = ctx.device();
        allocator = ctx.allocator();
        return device != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE;
    }

    bool ensure_sampler()
    {
        if (sampler != VK_NULL_HANDLE)
            return true;

        VkSamplerCreateInfo sampler_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = 1.0f;
        if (vkCreateSampler(device, &sampler_info, nullptr, &sampler) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create sampler");
            return false;
        }
        return true;
    }

    bool ensure_descriptor_layouts()
    {
        if (rect_descriptor_layout != VK_NULL_HANDLE && glyph_descriptor_layout != VK_NULL_HANDLE)
            return true;

        if (rect_descriptor_layout == VK_NULL_HANDLE)
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding = 0;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

            VkDescriptorSetLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            layout_info.bindingCount = 1;
            layout_info.pBindings = &binding;
            if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &rect_descriptor_layout) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create rect descriptor layout");
                return false;
            }
        }

        if (glyph_descriptor_layout == VK_NULL_HANDLE)
        {
            VkDescriptorSetLayoutBinding bindings[2] = {};
            bindings[0].binding = 0;
            bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            bindings[1].binding = 1;
            bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            layout_info.bindingCount = 2;
            layout_info.pBindings = bindings;
            if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &glyph_descriptor_layout) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create glyph descriptor layout");
                return false;
            }
        }

        return true;
    }

    bool ensure_descriptor_pool()
    {
        if (descriptor_pool != VK_NULL_HANDLE)
            return true;

        VkDescriptorPoolSize pool_sizes[2] = {};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[0].descriptorCount = kDescriptorPoolMaxSets;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = kDescriptorPoolMaxSets;

        VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_info.maxSets = kDescriptorPoolMaxSets;
        pool_info.poolSizeCount = 2;
        pool_info.pPoolSizes = pool_sizes;
        if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create descriptor pool");
            return false;
        }
        return true;
    }

    bool allocate_descriptor_set(VkDescriptorSetLayout layout, VkDescriptorSet& set)
    {
        if (set != VK_NULL_HANDLE)
            return true;
        if (!ensure_descriptor_pool())
            return false;

        VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc_info.descriptorPool = descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &layout;
        if (vkAllocateDescriptorSets(device, &alloc_info, &set) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to allocate descriptor set");
            return false;
        }
        return true;
    }

    bool ensure_frame_resources(uint32_t requested_count)
    {
        requested_count = std::max(1u, requested_count);
        if (!ensure_descriptor_layouts())
            return false;
        if (buffered_frame_count == requested_count && frames.size() == requested_count)
            return true;

        vkDeviceWaitIdle(device);
        if (descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;

        for (auto& frame : frames)
        {
            destroy_buffer(allocator, frame.rect_buffer);
            destroy_buffer(allocator, frame.glyph_buffer);
            destroy_buffer(allocator, frame.atlas_staging);
        }
        frames.clear();
        frames.resize(requested_count);
        buffered_frame_count = requested_count;
        return true;
    }

    FrameResources& frame_for(const VkRenderContext& ctx)
    {
        return frames[ctx.frame_index() % frames.size()];
    }

    bool ensure_pipeline_layouts()
    {
        if (rect_pipeline_layout != VK_NULL_HANDLE && glyph_pipeline_layout != VK_NULL_HANDLE)
            return true;
        if (!ensure_descriptor_layouts())
            return false;

        VkPushConstantRange push_range = {};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(PushConstants);

        if (rect_pipeline_layout == VK_NULL_HANDLE)
        {
            VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            layout_info.setLayoutCount = 1;
            layout_info.pSetLayouts = &rect_descriptor_layout;
            layout_info.pushConstantRangeCount = 1;
            layout_info.pPushConstantRanges = &push_range;
            if (vkCreatePipelineLayout(device, &layout_info, nullptr, &rect_pipeline_layout) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create rect pipeline layout");
                return false;
            }
        }

        if (glyph_pipeline_layout == VK_NULL_HANDLE)
        {
            VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            layout_info.setLayoutCount = 1;
            layout_info.pSetLayouts = &glyph_descriptor_layout;
            layout_info.pushConstantRangeCount = 1;
            layout_info.pPushConstantRanges = &push_range;
            if (vkCreatePipelineLayout(device, &layout_info, nullptr, &glyph_pipeline_layout) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create glyph pipeline layout");
                return false;
            }
        }

        return true;
    }

    bool create_pipeline(
        const char* vert_name,
        const char* frag_name,
        VkPipelineLayout layout,
        VkPipeline& pipeline)
    {
        const auto shader_dir = bundled_asset_path("shaders");
        VkShaderModule vert = load_shader(device, shader_dir / vert_name);
        VkShaderModule frag = load_shader(device, shader_dir / frag_name);
        if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE)
        {
            if (vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, vert, nullptr);
            if (frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, frag, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VkPipelineInputAssemblyStateCreateInfo input_assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport_state = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depth.depthTestEnable = VK_FALSE;
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic_state = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dynamic_states;

        VkPipelineColorBlendAttachmentState blend_attachment = {};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        VkGraphicsPipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &raster;
        pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pDepthStencilState = &depth;
        pipeline_info.pColorBlendState = &blend;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = layout;
        pipeline_info.renderPass = render_pass;
        pipeline_info.subpass = 0;

        const bool ok =
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) == VK_SUCCESS;
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        if (!ok)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create pipeline for %s / %s", vert_name, frag_name);
            return false;
        }
        return true;
    }

    bool ensure_pipelines(const VkRenderContext& ctx)
    {
        if (!ensure_pipeline_layouts())
            return false;

        if (render_pass != ctx.render_pass())
        {
            destroy_pipelines();
            render_pass = ctx.render_pass();
        }
        if (render_pass == VK_NULL_HANDLE)
            return false;

        if (rect_pipeline == VK_NULL_HANDLE
            && !create_pipeline("markdown_rect.vert.spv", "markdown_rect.frag.spv", rect_pipeline_layout, rect_pipeline))
        {
            return false;
        }
        if (glyph_pipeline == VK_NULL_HANDLE
            && !create_pipeline("markdown_glyph.vert.spv", "markdown_glyph.frag.spv", glyph_pipeline_layout, glyph_pipeline))
        {
            return false;
        }
        return true;
    }

    bool upload_instances(const MarkdownDrawList& draw_list, FrameResources& frame, uint64_t revision)
    {
        if (frame.uploaded_draw_revision == revision)
            return true;

        const size_t rect_bytes = draw_list.rects.size() * sizeof(MarkdownRectInstance);
        if (rect_bytes > 0)
        {
            if (!ensure_mapped_buffer(
                    device,
                    allocator,
                    frame.rect_buffer,
                    rect_bytes,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    vkresources::LifetimeScope::Frame,
                    "markdown rect instance"))
            {
                return false;
            }
            if (!upload_to_mapped_buffer(allocator, frame.rect_buffer, draw_list.rects.data(), rect_bytes))
                return false;
        }

        const size_t glyph_bytes = draw_list.glyphs.size() * sizeof(MarkdownGlyphInstance);
        if (glyph_bytes > 0)
        {
            if (!ensure_mapped_buffer(
                    device,
                    allocator,
                    frame.glyph_buffer,
                    glyph_bytes,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    vkresources::LifetimeScope::Frame,
                    "markdown glyph instance"))
            {
                return false;
            }
            if (!upload_to_mapped_buffer(allocator, frame.glyph_buffer, draw_list.glyphs.data(), glyph_bytes))
                return false;
        }

        frame.uploaded_draw_revision = revision;
        return true;
    }

    bool ensure_atlas_texture(AtlasTexture& atlas, int width, int height)
    {
        if (width <= 0 || height <= 0)
            return false;
        if (atlas.image.image != VK_NULL_HANDLE && atlas.image.width == width && atlas.image.height == height)
            return true;

        if (atlas.image.image != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(device);
            destroy_image(device, allocator, atlas.image);
            atlas.generation = 0;
            atlas.upload_revision = 0;
        }

        VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            1,
        };
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkresources::ScopedImage replacement;
        std::string error;
        if (!vkresources::create_image(device, allocator,
                vkresources::ImageRequest(image_info, VK_IMAGE_VIEW_TYPE_2D,
                    VK_IMAGE_ASPECT_COLOR_BIT, vkresources::MemoryPolicy::DevicePreferred,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, "markdown.atlas",
                    vkresources::LifetimeScope::Persistent),
                replacement, error))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create atlas image: %s", error.c_str());
            return false;
        }
        const vkresources::ImageResource created = replacement.release();
        atlas.image.image = created.image;
        atlas.image.allocation = created.allocation;
        atlas.image.view = created.view;

        atlas.image.width = width;
        atlas.image.height = height;
        atlas.image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    }

    bool prepare_upload_copies(const std::vector<MarkdownAtlasUpload>& uploads, std::vector<UploadCopy>& copies)
    {
        copies.clear();
        VkDeviceSize staging_offset = 0;
        for (const auto& upload : uploads)
        {
            if (upload.atlas_id == 0 || upload.atlas_width <= 0 || upload.atlas_height <= 0)
                continue;

            auto& atlas = atlases[upload.atlas_id];
            if (atlas.image.image != VK_NULL_HANDLE && atlas.generation == upload.generation
                && upload.upload_revision <= atlas.upload_revision)
            {
                continue;
            }

            UploadCopy copy;
            copy.upload = &upload;
            if (upload.full_upload)
            {
                copy.x = 0;
                copy.y = 0;
                copy.width = upload.atlas_width;
                copy.height = upload.atlas_height;
            }
            else
            {
                copy.x = std::clamp(upload.rect.pos.x, 0, upload.atlas_width);
                copy.y = std::clamp(upload.rect.pos.y, 0, upload.atlas_height);
                copy.width = std::clamp(upload.rect.size.x, 0, upload.atlas_width - copy.x);
                copy.height = std::clamp(upload.rect.size.y, 0, upload.atlas_height - copy.y);
            }

            if (copy.width <= 0 || copy.height <= 0)
                continue;

            copy.bytes = static_cast<size_t>(copy.width) * static_cast<size_t>(copy.height) * kAtlasChannels;
            if (upload.rgba.size() < copy.bytes)
            {
                DRAXUL_LOG_WARN(
                    LogCategory::Renderer,
                    "Markdown: atlas upload %u has %zu bytes, expected at least %zu",
                    upload.atlas_id,
                    upload.rgba.size(),
                    copy.bytes);
                continue;
            }

            if (!ensure_atlas_texture(atlas, upload.atlas_width, upload.atlas_height))
                return false;

            copy.staging_offset = staging_offset;
            staging_offset += static_cast<VkDeviceSize>(copy.bytes);
            copies.push_back(copy);
        }
        return true;
    }

    bool upload_atlases(
        const VkRenderContext& ctx,
        FrameResources& frame,
        const std::vector<MarkdownAtlasUpload>& uploads)
    {
        if (uploads.empty())
            return true;

        std::vector<UploadCopy> copies;
        if (!prepare_upload_copies(uploads, copies))
            return false;
        if (copies.empty())
            return true;

        VkDeviceSize total_bytes = 0;
        for (const auto& copy : copies)
            total_bytes += static_cast<VkDeviceSize>(copy.bytes);

        if (!ensure_mapped_buffer(
                device,
                allocator,
                frame.atlas_staging,
                total_bytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                vkresources::LifetimeScope::Frame,
                "markdown atlas staging"))
        {
            return false;
        }

        auto* dst = static_cast<uint8_t*>(frame.atlas_staging.mapped);
        for (const auto& copy : copies)
            std::memcpy(dst + copy.staging_offset, copy.upload->rgba.data(), copy.bytes);
        vmaFlushAllocation(allocator, frame.atlas_staging.allocation, 0, total_bytes);

        VkCommandBuffer cmd = ctx.command_buffer();
        for (const auto& copy : copies)
        {
            auto atlas_it = atlases.find(copy.upload->atlas_id);
            if (atlas_it == atlases.end())
                return false;

            auto& atlas = atlas_it->second;
            VkAccessFlags src_access = 0;
            VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            if (atlas.image.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                src_access = VK_ACCESS_SHADER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }

            transition_image(cmd, atlas.image.image,
                atlas.image.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                src_access, VK_ACCESS_TRANSFER_WRITE_BIT,
                src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkBufferImageCopy region = {};
            region.bufferOffset = copy.staging_offset;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { copy.x, copy.y, 0 };
            region.imageExtent = {
                static_cast<uint32_t>(copy.width),
                static_cast<uint32_t>(copy.height),
                1,
            };
            vkCmdCopyBufferToImage(cmd, frame.atlas_staging.buffer, atlas.image.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            transition_image(cmd, atlas.image.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            atlas.image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            atlas.generation = copy.upload->generation;
            atlas.upload_revision = std::max(atlas.upload_revision, copy.upload->upload_revision);
        }

        return true;
    }

    bool update_rect_descriptor(FrameResources& frame)
    {
        if (frame.rect_buffer.buffer == VK_NULL_HANDLE)
            return false;
        if (!allocate_descriptor_set(rect_descriptor_layout, frame.rect_descriptor_set))
            return false;

        VkDescriptorBufferInfo buffer_info = {};
        buffer_info.buffer = frame.rect_buffer.buffer;
        buffer_info.offset = 0;
        buffer_info.range = frame.rect_buffer.size;

        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = frame.rect_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buffer_info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        return true;
    }

    bool update_glyph_descriptor(FrameResources& frame, uint32_t atlas_id, const AtlasTexture& atlas, VkDescriptorSet& set)
    {
        if (frame.glyph_buffer.buffer == VK_NULL_HANDLE || atlas.image.view == VK_NULL_HANDLE
            || sampler == VK_NULL_HANDLE || atlas.image.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            return false;
        }
        if (!allocate_descriptor_set(glyph_descriptor_layout, set))
            return false;

        VkDescriptorBufferInfo buffer_info = {};
        buffer_info.buffer = frame.glyph_buffer.buffer;
        buffer_info.offset = 0;
        buffer_info.range = frame.glyph_buffer.size;

        VkDescriptorImageInfo image_info = {};
        image_info.sampler = sampler;
        image_info.imageView = atlas.image.view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[2] = {};
        writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        writes[0].dstSet = set;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo = &buffer_info;
        writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        writes[1].dstSet = set;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &image_info;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        (void)atlas_id;
        return true;
    }
};

MarkdownRenderPass::MarkdownRenderPass() = default;

MarkdownRenderPass::~MarkdownRenderPass()
{
    if (state_)
        state_->shutdown();
}

MarkdownRenderPass::MarkdownRenderPass(MarkdownRenderPass&&) noexcept = default;
MarkdownRenderPass& MarkdownRenderPass::operator=(MarkdownRenderPass&& other) noexcept
{
    if (this == &other)
        return *this;

    if (state_)
        state_->shutdown();
    state_ = std::move(other.state_);
    draw_list_ = std::move(other.draw_list_);
    atlas_uploads_ = std::move(other.atlas_uploads_);
    draw_revision_ = other.draw_revision_;
    other.draw_revision_ = 0;
    return *this;
}

void MarkdownRenderPass::record_prepass(IRenderContext& ctx)
{
    if (!draw_list_.has_work() && atlas_uploads_.empty())
        return;

    auto& vk_ctx = static_cast<VkRenderContext&>(ctx);
    if (!state_)
        state_ = std::make_unique<State>();
    if (!state_->ensure_device(vk_ctx) || !state_->ensure_sampler()
        || !state_->ensure_frame_resources(vk_ctx.buffered_frame_count()))
    {
        return;
    }

    auto& frame = state_->frame_for(vk_ctx);
    const bool instances_uploaded = state_->upload_instances(draw_list_, frame, draw_revision_);
    const bool atlases_uploaded = state_->upload_atlases(vk_ctx, frame, atlas_uploads_);
    if (instances_uploaded && atlases_uploaded)
        atlas_uploads_.clear();
}

void MarkdownRenderPass::record(IRenderContext& ctx)
{
    if (!state_ || !draw_list_.has_work())
        return;

    auto& vk_ctx = static_cast<VkRenderContext&>(ctx);
    if (!state_->ensure_device(vk_ctx) || !state_->ensure_sampler()
        || !state_->ensure_frame_resources(vk_ctx.buffered_frame_count())
        || !state_->ensure_pipelines(vk_ctx))
    {
        return;
    }

    auto& frame = state_->frame_for(vk_ctx);
    if (frame.uploaded_draw_revision != draw_revision_)
        return;

    const int screen_w = std::max(1, vk_ctx.width());
    const int screen_h = std::max(1, vk_ctx.height());
    const int viewport_x = vk_ctx.viewport_x();
    const int viewport_y = vk_ctx.viewport_y();
    const int viewport_w = vk_ctx.viewport_w();
    const int viewport_h = vk_ctx.viewport_h();

    VkCommandBuffer cmd = vk_ctx.command_buffer();
    VkViewport viewport = {};
    viewport.width = static_cast<float>(screen_w);
    viewport.height = static_cast<float>(screen_h);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = std::max(0, viewport_x);
    scissor.offset.y = std::max(0, viewport_y);
    scissor.extent.width = static_cast<uint32_t>(clamped_extent(viewport_x, viewport_w, screen_w));
    scissor.extent.height = static_cast<uint32_t>(clamped_extent(viewport_y, viewport_h, screen_h));
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    PushConstants push;
    push.screen_w = static_cast<float>(screen_w);
    push.screen_h = static_cast<float>(screen_h);
    push.viewport_x = static_cast<float>(viewport_x);
    push.viewport_y = static_cast<float>(viewport_y);

    if (!draw_list_.rects.empty() && state_->update_rect_descriptor(frame))
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->rect_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->rect_pipeline_layout,
            0, 1, &frame.rect_descriptor_set, 0, nullptr);
        vkCmdPushConstants(cmd, state_->rect_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, static_cast<uint32_t>(draw_list_.rects.size()), 0, 0);
    }

    if (draw_list_.glyphs.empty() || draw_list_.glyph_batches.empty() || frame.glyph_buffer.buffer == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->glyph_pipeline);
    vkCmdPushConstants(cmd, state_->glyph_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

    for (const auto& batch : draw_list_.glyph_batches)
    {
        if (batch.count == 0 || !instance_range_valid(draw_list_.glyphs, batch.first, batch.count))
            continue;

        auto atlas_it = state_->atlases.find(batch.atlas_id);
        if (atlas_it == state_->atlases.end() || atlas_it->second.generation != batch.atlas_generation)
            continue;

        VkDescriptorSet& descriptor_set = frame.glyph_descriptor_sets[batch.atlas_id];
        if (!state_->update_glyph_descriptor(frame, batch.atlas_id, atlas_it->second, descriptor_set))
            continue;

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->glyph_pipeline_layout,
            0, 1, &descriptor_set, 0, nullptr);
        vkCmdDraw(cmd, 6, batch.count, 0, batch.first);
    }
}

} // namespace draxul::markdown
