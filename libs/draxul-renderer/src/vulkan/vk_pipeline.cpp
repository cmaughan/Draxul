#include "vk_pipeline.h"

#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/runtime_path.h>
#include <fstream>
#include <utility>
#include <vector>

namespace draxul
{

VkShaderModule VkPipelineManager::load_shader(VkDevice device, const std::string& path)
{
    PERF_MEASURE();
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to open shader: %s", path.c_str());
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
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create shader module: %s", path.c_str());
        return VK_NULL_HANDLE;
    }
    return shader_module;
}

bool VkPipelineManager::initialize(VkDevice device, VkRenderPass render_pass, const std::string& shader_dir)
{
    PERF_MEASURE();
    const auto resolved_shader_dir = bundled_asset_path(shader_dir);
    auto bg_vert = load_shader(device, (resolved_shader_dir / "grid_bg.vert.spv").string());
    auto bg_frag = load_shader(device, (resolved_shader_dir / "grid_bg.frag.spv").string());
    auto fg_vert = load_shader(device, (resolved_shader_dir / "grid_fg.vert.spv").string());
    auto fg_frag = load_shader(device, (resolved_shader_dir / "grid_fg.frag.spv").string());

    if (!bg_vert || !bg_frag || !fg_vert || !fg_frag)
    {
        if (bg_vert)
            vkDestroyShaderModule(device, bg_vert, nullptr);
        if (bg_frag)
            vkDestroyShaderModule(device, bg_frag, nullptr);
        if (fg_vert)
            vkDestroyShaderModule(device, fg_vert, nullptr);
        if (fg_frag)
            vkDestroyShaderModule(device, fg_frag, nullptr);
        return false;
    }

    VkPipelineManager pending;
    auto cleanup = [&]() {
        pending.shutdown(device);
        vkDestroyShaderModule(device, bg_vert, nullptr);
        vkDestroyShaderModule(device, bg_frag, nullptr);
        vkDestroyShaderModule(device, fg_vert, nullptr);
        vkDestroyShaderModule(device, fg_frag, nullptr);
    };

    VkPushConstantRange push_range = {};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(float) * 7;

    {
        VkDescriptorSetLayoutBinding ssbo_binding = {};
        ssbo_binding.binding = 0;
        ssbo_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ssbo_binding.descriptorCount = 1;
        ssbo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_ci.bindingCount = 1;
        layout_ci.pBindings = &ssbo_binding;
        if (vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &pending.bg_desc_layout_) != VK_SUCCESS)
        {
            cleanup();
            return false;
        }
    }

    {
        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_ci.bindingCount = 2;
        layout_ci.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &pending.fg_desc_layout_) != VK_SUCCESS)
        {
            cleanup();
            return false;
        }
    }

    {
        VkPipelineLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layout_ci.setLayoutCount = 1;
        layout_ci.pSetLayouts = &pending.bg_desc_layout_;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_range;
        if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &pending.bg_layout_) != VK_SUCCESS)
        {
            cleanup();
            return false;
        }

        layout_ci.pSetLayouts = &pending.fg_desc_layout_;
        if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &pending.fg_layout_) != VK_SUCCESS)
        {
            cleanup();
            return false;
        }
    }

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
    depth.depthBoundsTestEnable = VK_FALSE;
    depth.stencilTestEnable = VK_FALSE;
    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    {
        VkPipelineColorBlendAttachmentState blend_attachment = {};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_attachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = bg_vert;
        stages[0].pName = "main";
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = bg_frag;
        stages[1].pName = "main";

        VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vertex_input;
        pi.pInputAssemblyState = &input_assembly;
        pi.pViewportState = &viewport_state;
        pi.pRasterizationState = &raster;
        pi.pMultisampleState = &multisample;
        pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &blend;
        pi.pDynamicState = &dynamic_state;
        pi.layout = pending.bg_layout_;
        pi.renderPass = render_pass;
        pi.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pending.bg_pipeline_) != VK_SUCCESS)
        {
            cleanup();
            return false;
        }
    }

    {
        VkPipelineColorBlendAttachmentState blend_attachment = {};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = fg_vert;
        stages[0].pName = "main";
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fg_frag;
        stages[1].pName = "main";

        VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vertex_input;
        pi.pInputAssemblyState = &input_assembly;
        pi.pViewportState = &viewport_state;
        pi.pRasterizationState = &raster;
        pi.pMultisampleState = &multisample;
        pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &blend;
        pi.pDynamicState = &dynamic_state;
        pi.layout = pending.fg_layout_;
        pi.renderPass = render_pass;
        pi.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pending.fg_pipeline_) != VK_SUCCESS)
        {
            cleanup();
            return false;
        }
    }

    vkDestroyShaderModule(device, bg_vert, nullptr);
    vkDestroyShaderModule(device, bg_frag, nullptr);
    vkDestroyShaderModule(device, fg_vert, nullptr);
    vkDestroyShaderModule(device, fg_frag, nullptr);

    reset_objects(device);
    swap(pending);
    pending.shutdown(device);
    return true;
}

void VkPipelineManager::shutdown(VkDevice device)
{
    PERF_MEASURE();
    reset_objects(device);
}

void VkPipelineManager::swap(VkPipelineManager& other) noexcept
{
    using std::swap;

    swap(bg_pipeline_, other.bg_pipeline_);
    swap(fg_pipeline_, other.fg_pipeline_);
    swap(bg_layout_, other.bg_layout_);
    swap(fg_layout_, other.fg_layout_);
    swap(bg_desc_layout_, other.bg_desc_layout_);
    swap(fg_desc_layout_, other.fg_desc_layout_);
}

void VkPipelineManager::reset_objects(VkDevice device)
{
    PERF_MEASURE();
    if (bg_pipeline_)
        vkDestroyPipeline(device, bg_pipeline_, nullptr);
    if (fg_pipeline_)
        vkDestroyPipeline(device, fg_pipeline_, nullptr);
    if (bg_layout_)
        vkDestroyPipelineLayout(device, bg_layout_, nullptr);
    if (fg_layout_)
        vkDestroyPipelineLayout(device, fg_layout_, nullptr);
    if (bg_desc_layout_)
        vkDestroyDescriptorSetLayout(device, bg_desc_layout_, nullptr);
    if (fg_desc_layout_)
        vkDestroyDescriptorSetLayout(device, fg_desc_layout_, nullptr);

    bg_pipeline_ = VK_NULL_HANDLE;
    fg_pipeline_ = VK_NULL_HANDLE;
    bg_layout_ = VK_NULL_HANDLE;
    fg_layout_ = VK_NULL_HANDLE;
    bg_desc_layout_ = VK_NULL_HANDLE;
    fg_desc_layout_ = VK_NULL_HANDLE;
}

} // namespace draxul
