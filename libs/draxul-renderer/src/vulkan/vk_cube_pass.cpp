#include "vk_cube_pass.h"

#include <draxul/log.h>
#include <fstream>
#include <vector>

namespace draxul
{

namespace
{

VkShaderModule load_cube_shader(VkDevice device, const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "VkCubePass: failed to open shader: %s", path.c_str());
        return VK_NULL_HANDLE;
    }

    size_t size = (size_t)file.tellg();
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), size);

    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = size;
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shader_module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &shader_module) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "VkCubePass: failed to create shader module: %s", path.c_str());
        return VK_NULL_HANDLE;
    }
    return shader_module;
}

} // namespace

bool VkCubePass::create(VkDevice device, VkRenderPass render_pass, const std::string& shader_dir)
{
    auto vert = load_cube_shader(device, shader_dir + "/megacity_cube.vert.spv");
    auto frag = load_cube_shader(device, shader_dir + "/megacity_cube.frag.spv");
    if (!vert || !frag)
    {
        if (vert)
            vkDestroyShaderModule(device, vert, nullptr);
        if (frag)
            vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    // Push constant: 64 bytes — mat4 MVP
    VkPushConstantRange push_range = {};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = 64;

    VkPipelineLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layout_ci.pushConstantRangeCount = 1;
    layout_ci.pPushConstantRanges = &push_range;
    if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &layout) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "VkCubePass: failed to create pipeline layout");
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // No vertex buffer — all 36 vertices are baked into the vertex shader
    VkPipelineVertexInputStateCreateInfo vert_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // Y-flip in proj preserves CCW winding
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    // Render pass has no depth attachment; depth test disabled

    VkPipelineColorBlendAttachmentState blend_att = {};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_att;

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo pipe_ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipe_ci.stageCount = 2;
    pipe_ci.pStages = stages;
    pipe_ci.pVertexInputState = &vert_input;
    pipe_ci.pInputAssemblyState = &input_assembly;
    pipe_ci.pViewportState = &viewport_state;
    pipe_ci.pRasterizationState = &raster;
    pipe_ci.pMultisampleState = &ms;
    pipe_ci.pDepthStencilState = &depth;
    pipe_ci.pColorBlendState = &blend;
    pipe_ci.pDynamicState = &dynamic;
    pipe_ci.layout = layout;
    pipe_ci.renderPass = render_pass;
    pipe_ci.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_ci, nullptr, &pipeline);
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);

    if (result != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "VkCubePass: failed to create graphics pipeline");
        vkDestroyPipelineLayout(device, layout, nullptr);
        layout = VK_NULL_HANDLE;
        return false;
    }

    DRAXUL_LOG_INFO(LogCategory::Renderer, "VkCubePass: cube pipeline created");
    return true;
}

void VkCubePass::destroy(VkDevice device)
{
    if (pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, layout, nullptr);
        layout = VK_NULL_HANDLE;
    }
}

} // namespace draxul
