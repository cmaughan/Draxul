#include <draxul/vulkan/vk_hdr_scene_pipeline.h>

#include <array>
#include <draxul/log.h>

namespace draxul::vkresources
{
namespace
{

// ---- THE subpass dependency masks -------------------------------------------
//
// MegaCity and SatView disagreed here (audit cluster C). Both scene passes used
// a first dependency whose source stage was TOP_OF_PIPE (SatView) or
// BOTTOM_OF_PIPE (MegaCity) with an empty srcAccessMask — i.e. an execution
// dependency that orders nothing. That is fine for the very first use of a
// freshly created image, but these attachments are per-frame-in-flight and
// REUSED: the previous frame's tone-map/present pass samples scene_hdr and
// scene_final in the fragment stage, and this frame's scene pass writes the same
// images as colour/resolve attachments. That is a write-after-read hazard which
// neither product's mask actually covered.
//
// The masks below are the strict superset that is correct for both usages:
//
//   entry (EXTERNAL -> subpass 0)
//     src = FRAGMENT_SHADER / SHADER_READ
//         the prior frame's tone-map and present passes sampling these images,
//         plus MegaCity's AO pass sampling the g-buffer.
//     dst = COLOR_ATTACHMENT_OUTPUT | EARLY_FRAGMENT_TESTS
//           / COLOR_ATTACHMENT_WRITE | DEPTH_STENCIL_ATTACHMENT_WRITE
//         both products' loadOp=CLEAR colour and depth writes.
//
//   exit (subpass 0 -> EXTERNAL)
//     src = COLOR_ATTACHMENT_OUTPUT | LATE_FRAGMENT_TESTS
//           / COLOR_ATTACHMENT_WRITE | DEPTH_STENCIL_ATTACHMENT_WRITE
//         MegaCity's mask; the depth half matters wherever depth is sampled
//         downstream and is harmless where it is not.
//     dst = FRAGMENT_SHADER / SHADER_READ
//         the tone-map pass sampling the resolve target.
//
// Every stage named is a graphics-pipeline stage present in both products'
// queues, and no deprecated TOP_OF_PIPE/BOTTOM_OF_PIPE source appears, so the
// validation layers accept this for both usages.
std::array<VkSubpassDependency, 2> scene_dependencies()
{
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    return dependencies;
}

// Colour-only variant of the same idea; both products already agreed on these.
std::array<VkSubpassDependency, 2> color_dependencies()
{
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    return dependencies;
}

bool create_scene_render_pass(VkDevice device, const HdrScenePipelineConfig& config,
    VkSampleCountFlagBits samples, bool preserve_msaa, VkRenderPass& output, std::string& error)
{
    const bool multisampled = samples != VK_SAMPLE_COUNT_1_BIT;

    std::array<VkAttachmentDescription, 3> attachments{};
    uint32_t attachment_count = 0;

    attachments[0].format = config.color_format;
    attachments[0].samples = samples;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = multisampled
        ? (preserve_msaa ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE)
        : VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = multisampled
        ? (preserve_msaa ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                         : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment_count = 1;

    attachments[1].format = config.depth_format;
    attachments[1].samples = samples;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachment_count = 2;

    // At 1x there is no resolve attachment and attachment 0 IS the sampled
    // output. MegaCity's copy declared the resolve unconditionally, which is
    // invalid at 1x — adopting this pass makes its MSAA fallback path legal.
    VkAttachmentReference resolve_ref{};
    if (multisampled)
    {
        attachments[2].format = config.color_format;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        resolve_ref.attachment = 2;
        resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment_count = 3;
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;
    subpass.pResolveAttachments = multisampled ? &resolve_ref : nullptr;

    const auto dependencies = scene_dependencies();
    VkRenderPassCreateInfo create_info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    create_info.attachmentCount = attachment_count;
    create_info.pAttachments = attachments.data();
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = static_cast<uint32_t>(dependencies.size());
    create_info.pDependencies = dependencies.data();
    const VkResult result = vkCreateRenderPass(device, &create_info, nullptr, &output);
    if (result != VK_SUCCESS)
    {
        error = "HDR scene render pass creation failed (" + std::to_string(result) + ")";
        return false;
    }
    error.clear();
    return true;
}

} // namespace

bool create_color_render_pass(VkDevice device, VkFormat format, VkAttachmentLoadOp load_op,
    VkRenderPass& output, std::string& error)
{
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = load_op;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    const auto dependencies = color_dependencies();
    VkRenderPassCreateInfo create_info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    create_info.attachmentCount = 1;
    create_info.pAttachments = &attachment;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = static_cast<uint32_t>(dependencies.size());
    create_info.pDependencies = dependencies.data();
    const VkResult result = vkCreateRenderPass(device, &create_info, nullptr, &output);
    if (result != VK_SUCCESS)
    {
        error = "colour render pass creation failed (" + std::to_string(result) + ")";
        return false;
    }
    error.clear();
    return true;
}

bool create_fullscreen_pipeline(VkDevice device, const FullscreenPipelineRequest& request,
    VkPipeline& output, std::string& error)
{
    if (request.vertex_shader == VK_NULL_HANDLE || request.fragment_shader == VK_NULL_HANDLE
        || request.layout == VK_NULL_HANDLE || request.render_pass == VK_NULL_HANDLE)
    {
        error = "fullscreen pipeline requires both shaders, a layout and a render pass";
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = request.vertex_shader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = request.fragment_shader;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport_state{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
    };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth_stencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    depth_stencil.depthTestEnable = VK_FALSE;
    depth_stencil.depthWriteEnable = VK_FALSE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;
    std::array dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
    };
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();

    VkGraphicsPipelineCreateInfo create_info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    create_info.stageCount = static_cast<uint32_t>(stages.size());
    create_info.pStages = stages.data();
    create_info.pVertexInputState = &vertex_input;
    create_info.pInputAssemblyState = &input_assembly;
    create_info.pViewportState = &viewport_state;
    create_info.pRasterizationState = &raster;
    create_info.pMultisampleState = &multisample;
    // A product's fullscreen present pass can target the host continuation
    // render pass, whose subpass includes a depth attachment. Vulkan requires
    // a valid state object in that case even though depth testing is disabled.
    create_info.pDepthStencilState = &depth_stencil;
    create_info.pColorBlendState = &blend;
    create_info.pDynamicState = &dynamic;
    create_info.layout = request.layout;
    create_info.renderPass = request.render_pass;
    create_info.subpass = 0;
    const VkResult result = vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &create_info, nullptr, &output);
    if (result != VK_SUCCESS)
    {
        error = "fullscreen pipeline creation failed (" + std::to_string(result) + ")";
        return false;
    }
    error.clear();
    return true;
}

bool HdrScenePipeline::create(VkPhysicalDevice physical_device, VkDevice device,
    const HdrScenePipelineConfig& config, std::string& error)
{
    if (physical_device == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
    {
        error = "HDR scene pipeline requires a physical device and device";
        return false;
    }

    destroy(device);
    config_ = config;
    debug_name_ = std::string(config.debug_name);
    config_.debug_name = debug_name_;
    sample_count_ = choose_scene_sample_count(
        physical_device, config.color_format, config.depth_format);

    if (!create_scene_render_pass(device, config_, sample_count_, false, scene_render_pass_, error))
        return false;
    if (config_.want_msaa_preserving_pass && multisampled()
        && !create_scene_render_pass(device, config_, sample_count_, true,
            scene_msaa_preserving_render_pass_, error))
        return false;
    if (!create_color_render_pass(device, config_.tone_mapped_format, config_.tone_map_load_op,
            tone_map_render_pass_, error))
        return false;

    DRAXUL_LOG_INFO(LogCategory::Renderer, "%s: HDR scene using %ux MSAA",
        debug_name_.c_str(), static_cast<uint32_t>(sample_count_));
    error.clear();
    return true;
}

void HdrScenePipeline::destroy(VkDevice device)
{
    if (device != VK_NULL_HANDLE)
    {
        if (scene_render_pass_ != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, scene_render_pass_, nullptr);
        if (scene_msaa_preserving_render_pass_ != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, scene_msaa_preserving_render_pass_, nullptr);
        if (tone_map_render_pass_ != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, tone_map_render_pass_, nullptr);
    }
    scene_render_pass_ = VK_NULL_HANDLE;
    scene_msaa_preserving_render_pass_ = VK_NULL_HANDLE;
    tone_map_render_pass_ = VK_NULL_HANDLE;
    sample_count_ = VK_SAMPLE_COUNT_1_BIT;
}

bool HdrScenePipeline::create_targets(VkDevice device, VmaAllocator allocator, int width,
    int height, HdrSceneTargets& targets, std::string& error) const
{
    if (!valid() || width <= 0 || height <= 0)
    {
        error = "HDR scene targets need a created pipeline and a positive extent";
        return false;
    }

    destroy_hdr_scene_targets(device, allocator, targets);
    const std::string prefix = debug_name_.empty() ? std::string("hdr") : debug_name_;
    const std::string msaa_name = prefix + ".hdr.scene-msaa";
    const std::string depth_name = prefix + ".hdr.scene-depth";
    const std::string resolve_name = prefix + ".hdr.scene-resolve";
    const std::string final_name = prefix + ".hdr.scene-final";
    const std::string final_view_name = final_name + ".unorm-view";

    if (multisampled()
        && !create_attachment(device, allocator,
            AttachmentRequest(width, height, config_.color_format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, sample_count_, 0,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, LifetimeScope::Frame, msaa_name),
            targets.scene_msaa, error))
        return false;
    if (!create_attachment(device, allocator,
            AttachmentRequest(width, height, config_.depth_format,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
                sample_count_, 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                LifetimeScope::Frame, depth_name),
            targets.scene_depth, error))
        return false;
    if (!create_attachment(device, allocator,
            AttachmentRequest(width, height, config_.color_format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, 0,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, LifetimeScope::Frame, resolve_name),
            targets.scene_hdr, error))
        return false;
    // MUTABLE_FORMAT so the sRGB attachment can also be sampled through a UNORM
    // view (ImGui/present read it without a second gamma application).
    if (!create_attachment(device, allocator,
            AttachmentRequest(width, height, config_.tone_mapped_format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT,
                VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                LifetimeScope::Frame, final_name),
            targets.scene_final, error))
        return false;
    if (!create_attachment_view(device, targets.scene_final.image, VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_ASPECT_COLOR_BIT, final_view_name, targets.scene_final_unorm_view, error))
        return false;

    std::array<VkImageView, 3> scene_views{};
    uint32_t scene_view_count = 0;
    scene_views[scene_view_count++] = multisampled()
        ? targets.scene_msaa.view
        : targets.scene_hdr.view;
    scene_views[scene_view_count++] = targets.scene_depth.view;
    if (multisampled())
        scene_views[scene_view_count++] = targets.scene_hdr.view;

    VkFramebufferCreateInfo scene_fb{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    scene_fb.renderPass = scene_render_pass_;
    scene_fb.attachmentCount = scene_view_count;
    scene_fb.pAttachments = scene_views.data();
    scene_fb.width = static_cast<uint32_t>(width);
    scene_fb.height = static_cast<uint32_t>(height);
    scene_fb.layers = 1;
    if (vkCreateFramebuffer(device, &scene_fb, nullptr, &targets.scene_framebuffer) != VK_SUCCESS)
    {
        error = "HDR scene framebuffer creation failed";
        return false;
    }

    VkFramebufferCreateInfo tone_map_fb{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    tone_map_fb.renderPass = tone_map_render_pass_;
    tone_map_fb.attachmentCount = 1;
    tone_map_fb.pAttachments = &targets.scene_final.view;
    tone_map_fb.width = static_cast<uint32_t>(width);
    tone_map_fb.height = static_cast<uint32_t>(height);
    tone_map_fb.layers = 1;
    if (vkCreateFramebuffer(device, &tone_map_fb, nullptr, &targets.tone_map_framebuffer)
        != VK_SUCCESS)
    {
        error = "HDR tone-map framebuffer creation failed";
        return false;
    }

    targets.width = width;
    targets.height = height;
    error.clear();
    return true;
}

void destroy_hdr_scene_targets(VkDevice device, VmaAllocator allocator, HdrSceneTargets& targets)
{
    if (device != VK_NULL_HANDLE)
    {
        if (targets.scene_framebuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, targets.scene_framebuffer, nullptr);
        if (targets.tone_map_framebuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, targets.tone_map_framebuffer, nullptr);
        if (targets.scene_final_unorm_view != VK_NULL_HANDLE)
            vkDestroyImageView(device, targets.scene_final_unorm_view, nullptr);
    }
    targets.scene_framebuffer = VK_NULL_HANDLE;
    targets.tone_map_framebuffer = VK_NULL_HANDLE;
    targets.scene_final_unorm_view = VK_NULL_HANDLE;
    destroy_attachment(device, allocator, targets.scene_msaa);
    destroy_attachment(device, allocator, targets.scene_depth);
    destroy_attachment(device, allocator, targets.scene_hdr);
    destroy_attachment(device, allocator, targets.scene_final);
    targets.width = 0;
    targets.height = 0;
}

} // namespace draxul::vkresources
