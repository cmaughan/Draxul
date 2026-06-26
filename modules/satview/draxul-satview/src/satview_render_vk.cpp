#include "satview_scene_pass.h"

#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/runtime_path.h>
#include <draxul/vulkan/vk_render_context.h>
#include <fstream>
#include <vector>

namespace draxul::satview
{

namespace
{

VkShaderModule load_shader(VkDevice device, const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to open shader %s", path.c_str());
        return VK_NULL_HANDLE;
    }

    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = size;
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create shader module %s", path.c_str());
        return VK_NULL_HANDLE;
    }
    return module;
}

glm::mat4 make_vulkan_projection(glm::mat4 view_proj)
{
    view_proj[1][1] *= -1.0f;
    return view_proj;
}

} // namespace

struct SatViewScenePass::State
{
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline earth_pipeline = VK_NULL_HANDLE;
    VkPipeline orbit_pipeline = VK_NULL_HANDLE;

    ~State()
    {
        destroy();
    }

    void destroy()
    {
        if (device != VK_NULL_HANDLE)
        {
            if (earth_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, earth_pipeline, nullptr);
            if (orbit_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, orbit_pipeline, nullptr);
            if (layout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(device, layout, nullptr);
        }
        earth_pipeline = VK_NULL_HANDLE;
        orbit_pipeline = VK_NULL_HANDLE;
        layout = VK_NULL_HANDLE;
        render_pass = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
    }

    bool ensure(VkDevice new_device, VkRenderPass new_render_pass)
    {
        if (earth_pipeline != VK_NULL_HANDLE && orbit_pipeline != VK_NULL_HANDLE
            && device == new_device && render_pass == new_render_pass)
            return true;

        destroy();
        device = new_device;
        render_pass = new_render_pass;
        if (device == VK_NULL_HANDLE || render_pass == VK_NULL_HANDLE)
            return false;

        const auto shader_dir = bundled_asset_path("shaders");
        VkShaderModule vert = load_shader(device, (shader_dir / "satview_earth.vert.spv").string());
        VkShaderModule frag = load_shader(device, (shader_dir / "satview_earth.frag.spv").string());
        VkShaderModule orbit_vert = load_shader(device, (shader_dir / "satview_orbit.vert.spv").string());
        VkShaderModule orbit_frag = load_shader(device, (shader_dir / "satview_orbit.frag.spv").string());
        if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE
            || orbit_vert == VK_NULL_HANDLE || orbit_frag == VK_NULL_HANDLE)
        {
            if (vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, vert, nullptr);
            if (frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, frag, nullptr);
            if (orbit_vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, orbit_vert, nullptr);
            if (orbit_frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, orbit_frag, nullptr);
            return false;
        }

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(SatViewFrameUniforms);

        VkPipelineLayoutCreateInfo layout_ci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_range;
        if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &layout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create pipeline layout");
            vkDestroyShaderModule(device, vert, nullptr);
            vkDestroyShaderModule(device, frag, nullptr);
            vkDestroyShaderModule(device, orbit_vert, nullptr);
            vkDestroyShaderModule(device, orbit_frag, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertex_input{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

        VkPipelineInputAssemblyStateCreateInfo input_assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport_state{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;

        VkGraphicsPipelineCreateInfo pipeline_ci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeline_ci.stageCount = 2;
        pipeline_ci.pStages = stages;
        pipeline_ci.pVertexInputState = &vertex_input;
        pipeline_ci.pInputAssemblyState = &input_assembly;
        pipeline_ci.pViewportState = &viewport_state;
        pipeline_ci.pRasterizationState = &raster;
        pipeline_ci.pMultisampleState = &multisample;
        pipeline_ci.pDepthStencilState = &depth;
        pipeline_ci.pColorBlendState = &blend;
        pipeline_ci.pDynamicState = &dynamic;
        pipeline_ci.layout = layout;
        pipeline_ci.renderPass = render_pass;
        pipeline_ci.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &earth_pipeline);
        if (result == VK_SUCCESS)
        {
            stages[0].module = orbit_vert;
            stages[1].module = orbit_frag;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            depth.depthWriteEnable = VK_FALSE;
            blend_attachment.blendEnable = VK_TRUE;
            blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &orbit_pipeline);
        }

        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        vkDestroyShaderModule(device, orbit_vert, nullptr);
        vkDestroyShaderModule(device, orbit_frag, nullptr);
        if (result != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create graphics pipeline");
            destroy();
            return false;
        }
        return true;
    }
};

SatViewScenePass::SatViewScenePass()
    : state_(std::make_unique<State>())
{
}

SatViewScenePass::~SatViewScenePass() = default;

void SatViewScenePass::record(IRenderContext& ctx)
{
    PERF_MEASURE();
    auto* vk_ctx = static_cast<VkRenderContext*>(&ctx);
    if (!state_->ensure(vk_ctx->device(), vk_ctx->render_pass()))
        return;

    SatViewFrameUniforms frame = frame_;
    frame.view_proj = make_vulkan_projection(frame.view_proj);

    VkCommandBuffer cmd = vk_ctx->command_buffer();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->earth_pipeline);
    vkCmdPushConstants(cmd, state_->layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(SatViewFrameUniforms), &frame);
    vkCmdDraw(cmd, kSatViewSphereVertexCount, 1, 0, 0);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->orbit_pipeline);
    vkCmdDraw(cmd, kSatViewOrbitVertexCount, 1, 0, 0);
}

} // namespace draxul::satview
