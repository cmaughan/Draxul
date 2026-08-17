#pragma once

// Shared scaffolding for the HDR scene pipeline both 3D products render with:
//
//     MSAA RGBA16F colour + D32 depth  ->  resolve to single-sample RGBA16F
//                                      ->  ACES tone map into BGRA8-sRGB
//                                      ->  present (sampled by the host pass)
//
// MegaCity and SatView each carried a full copy of the render-pass, attachment
// and tone-map-pipeline creation for this shape, and the two copies had already
// drifted apart in their subpass dependency masks (audit cluster C). This header
// owns ONE set of masks; see kSceneDependencies in the .cpp for the reasoning.
//
// Only the *scaffolding* lives here. Draw recording, descriptor layouts, scene
// pipelines and any product-specific extra passes stay in each product.

#include <draxul/vulkan/vk_resource_helpers.h>

#include <string>
#include <string_view>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace draxul::vkresources
{

struct HdrScenePipelineConfig
{
    // Formats the scene pass renders with. The MSAA sample count is probed
    // against these two formats (never device limits alone) — audit bug #7.
    VkFormat color_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    // Tone-mapped output. Written as sRGB, aliased UNORM for ImGui sampling.
    VkFormat tone_mapped_format = VK_FORMAT_B8G8R8A8_SRGB;
    // CLEAR is the conservative default. A fullscreen tone-map triangle covers
    // every pixel, so DONT_CARE is also correct (and is what MegaCity used).
    VkAttachmentLoadOp tone_map_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // Additional scene pass that STOREs the multisampled colour so a debug pass
    // can sample it (SatView's MSAA-difference view). Ignored at 1x.
    bool want_msaa_preserving_pass = false;
    // Prefix for Vulkan debug names and log lines, e.g. "megacity" / "satview".
    std::string_view debug_name;
};

// Per-frame render targets for the scene + tone-map passes. Deliberately just
// the attachments the two products genuinely share; each product keeps its own
// extra targets (MegaCity's g-buffer and shadow maps, SatView's MSAA-difference
// image) in its own per-frame struct alongside one of these.
struct HdrSceneTargets
{
    AttachmentResource scene_msaa; // multisampled colour; empty at 1x
    AttachmentResource scene_depth; // multisampled depth
    AttachmentResource scene_hdr; // single-sample resolve target
    AttachmentResource scene_final; // tone-mapped, sRGB view
    VkImageView scene_final_unorm_view = VK_NULL_HANDLE;
    VkFramebuffer scene_framebuffer = VK_NULL_HANDLE;
    VkFramebuffer tone_map_framebuffer = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
};

class HdrScenePipeline
{
public:
    HdrScenePipeline() = default;
    HdrScenePipeline(const HdrScenePipeline&) = delete;
    HdrScenePipeline& operator=(const HdrScenePipeline&) = delete;

    // Probes the sample count and creates the scene pass (+ optional
    // MSAA-preserving variant) and the tone-map pass.
    bool create(VkPhysicalDevice physical_device, VkDevice device,
        const HdrScenePipelineConfig& config, std::string& error);
    void destroy(VkDevice device);

    [[nodiscard]] bool valid() const
    {
        return scene_render_pass_ != VK_NULL_HANDLE;
    }
    [[nodiscard]] VkRenderPass scene_render_pass() const
    {
        return scene_render_pass_;
    }
    [[nodiscard]] VkRenderPass scene_msaa_preserving_render_pass() const
    {
        return scene_msaa_preserving_render_pass_;
    }
    [[nodiscard]] VkRenderPass tone_map_render_pass() const
    {
        return tone_map_render_pass_;
    }
    [[nodiscard]] VkSampleCountFlagBits sample_count() const
    {
        return sample_count_;
    }
    [[nodiscard]] bool multisampled() const
    {
        return sample_count_ != VK_SAMPLE_COUNT_1_BIT;
    }
    [[nodiscard]] const HdrScenePipelineConfig& config() const
    {
        return config_;
    }

    // Allocates the shared attachments and both framebuffers at `width`x`height`.
    bool create_targets(VkDevice device, VmaAllocator allocator, int width, int height,
        HdrSceneTargets& targets, std::string& error) const;

private:
    HdrScenePipelineConfig config_{};
    std::string debug_name_;
    VkSampleCountFlagBits sample_count_ = VK_SAMPLE_COUNT_1_BIT;
    VkRenderPass scene_render_pass_ = VK_NULL_HANDLE;
    VkRenderPass scene_msaa_preserving_render_pass_ = VK_NULL_HANDLE;
    VkRenderPass tone_map_render_pass_ = VK_NULL_HANDLE;
};

void destroy_hdr_scene_targets(VkDevice device, VmaAllocator allocator, HdrSceneTargets& targets);

// Single-subpass colour render pass with the shared "sample it, then write it,
// then sample it again" dependency masks. Used for the tone-map pass and for
// each product's own offscreen colour passes (MegaCity's AO buffer, SatView's
// MSAA-difference debug view), which previously carried identical hand copies.
bool create_color_render_pass(VkDevice device, VkFormat format, VkAttachmentLoadOp load_op,
    VkRenderPass& output, std::string& error);

// Fullscreen-triangle pipeline: no vertex input, dynamic viewport/scissor, no
// depth, no blending. Both products' tone-map, present and debug pipelines are
// this exact state vector.
struct FullscreenPipelineRequest
{
    FullscreenPipelineRequest(VkShaderModule requested_vertex_shader,
        VkShaderModule requested_fragment_shader, VkPipelineLayout requested_layout,
        VkRenderPass requested_render_pass)
        : vertex_shader(requested_vertex_shader)
        , fragment_shader(requested_fragment_shader)
        , layout(requested_layout)
        , render_pass(requested_render_pass)
    {
    }

    VkShaderModule vertex_shader;
    VkShaderModule fragment_shader;
    VkPipelineLayout layout;
    VkRenderPass render_pass;
};

bool create_fullscreen_pipeline(VkDevice device, const FullscreenPipelineRequest& request,
    VkPipeline& output, std::string& error);

} // namespace draxul::vkresources
