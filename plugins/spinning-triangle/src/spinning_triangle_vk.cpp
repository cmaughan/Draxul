#include <draxul/plugin_api.h>

#include <nlohmann/json.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

constexpr const char* kPluginId = "dev.draxul.spinning-triangle";
constexpr uint64_t kFrameDelayNs = 16'666'667;

struct PushConstants
{
    float angle = 0.0f;
    float aspect = 1.0f;
    int mode = 0;
};

struct TriangleInstance
{
    const DraxulPluginHostApiV1* host = nullptr;
    std::filesystem::path directory;
    DraxulPluginViewportV1 viewport{};
    float speed = 1.0f;
    float angle = 0.0f;
    float direction = 1.0f;
    double last_time = -1.0;
    bool paused = false;
    bool visible = true;
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    uint64_t target_generation = 0;
};

void log(TriangleInstance* instance, uint32_t level,
    const std::string& message)
{
    if (instance && instance->host && instance->host->log)
    {
        instance->host->log(instance->host->host_context,
            level, message.data(), message.size());
    }
}

std::vector<uint32_t> read_spirv(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return {};
    const auto size = input.tellg();
    if (size <= 0 || size % 4 != 0)
        return {};
    std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(words.data()), size);
    return input ? words : std::vector<uint32_t>{};
}

void destroy_pipeline(TriangleInstance& instance)
{
    if (instance.pipeline)
        vkDestroyPipeline(instance.device, instance.pipeline, nullptr);
    if (instance.pipeline_layout)
        vkDestroyPipelineLayout(instance.device,
            instance.pipeline_layout, nullptr);
    instance.pipeline = VK_NULL_HANDLE;
    instance.pipeline_layout = VK_NULL_HANDLE;
    instance.render_pass = VK_NULL_HANDLE;
}

bool create_pipeline(TriangleInstance& instance,
    const DraxulPluginVulkanFrameV1& frame, std::string& error)
{
    instance.device = static_cast<VkDevice>(frame.device);
    const auto vertex_words = read_spirv(
        instance.directory / "spinning_triangle.vert.spv");
    const auto fragment_words = read_spirv(
        instance.directory / "spinning_triangle.frag.spv");
    if (vertex_words.empty() || fragment_words.empty())
    {
        error = "Spinning Triangle shader assets are missing";
        return false;
    }

    const auto make_shader = [&](const std::vector<uint32_t>& words) {
        VkShaderModuleCreateInfo create_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        create_info.codeSize = words.size() * sizeof(uint32_t);
        create_info.pCode = words.data();
        VkShaderModule module = VK_NULL_HANDLE;
        return vkCreateShaderModule(instance.device, &create_info,
                   nullptr, &module)
                == VK_SUCCESS
            ? module : VK_NULL_HANDLE;
    };
    const VkShaderModule vertex = make_shader(vertex_words);
    const VkShaderModule fragment = make_shader(fragment_words);
    if (!vertex || !fragment)
    {
        if (vertex)
            vkDestroyShaderModule(instance.device, vertex, nullptr);
        if (fragment)
            vkDestroyShaderModule(instance.device, fragment, nullptr);
        error = "Spinning Triangle could not create shader modules";
        return false;
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo layout_info{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;
    if (vkCreatePipelineLayout(instance.device, &layout_info,
            nullptr, &instance.pipeline_layout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(instance.device, vertex, nullptr);
        vkDestroyShaderModule(instance.device, fragment, nullptr);
        error = "Spinning Triangle could not create pipeline layout";
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vertex_input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
        | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
        | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;
    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkGraphicsPipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &assembly;
    pipeline_info.pViewportState = &viewport;
    pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &blend;
    pipeline_info.pDynamicState = &dynamic;
    pipeline_info.layout = instance.pipeline_layout;
    pipeline_info.renderPass = reinterpret_cast<VkRenderPass>(
        static_cast<uintptr_t>(frame.continuation_render_pass));
    pipeline_info.subpass = 0;
    const VkResult result = vkCreateGraphicsPipelines(instance.device,
        VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &instance.pipeline);
    vkDestroyShaderModule(instance.device, vertex, nullptr);
    vkDestroyShaderModule(instance.device, fragment, nullptr);
    if (result != VK_SUCCESS)
    {
        destroy_pipeline(instance);
        error = "Spinning Triangle could not create graphics pipeline";
        return false;
    }
    instance.render_pass = pipeline_info.renderPass;
    instance.target_generation = frame.target_generation;
    return true;
}

DraxulPluginRenderResultV1 render_result(bool ok,
    uint64_t deadline, const char* error = nullptr)
{
    return { sizeof(DraxulPluginRenderResultV1), deadline,
        ok ? 1 : 0, error };
}

void* create_instance(const DraxulPluginCreateInfoV1* info)
{
    if (!info || !info->host)
        return nullptr;
    auto* instance = new TriangleInstance;
    instance->host = info->host;
    instance->directory = info->plugin_directory_utf8
        ? std::filesystem::u8path(info->plugin_directory_utf8)
        : std::filesystem::path{};
    instance->viewport = info->initial_viewport;
    try
    {
        const auto config = nlohmann::json::parse(
            info->config_json ? info->config_json : "{}",
            info->config_json
                ? info->config_json + info->config_json_length
                : "{}" + 2);
        instance->speed = config.value(
            "speed_radians_per_second", 1.0f);
        instance->angle = config.value("initial_angle", 0.0f);
        instance->paused = config.value("paused", false);
    }
    catch (...)
    {
        delete instance;
        return nullptr;
    }
    return instance;
}

void destroy_instance(void* opaque)
{
    auto* instance = static_cast<TriangleInstance*>(opaque);
    if (!instance)
        return;
    destroy_pipeline(*instance);
    delete instance;
}

void set_viewport(void* opaque, const DraxulPluginViewportV1* viewport)
{
    if (opaque && viewport)
        static_cast<TriangleInstance*>(opaque)->viewport = *viewport;
}

void set_visible(void* opaque, int32_t visible)
{
    auto* instance = static_cast<TriangleInstance*>(opaque);
    if (!instance)
        return;
    instance->visible = visible != 0;
    instance->last_time = -1.0;
    if (instance->visible && instance->host->request_redraw)
        instance->host->request_redraw(instance->host->host_context);
}

void set_focused(void*, int32_t) {}

int32_t handle_input(void* opaque,
    const DraxulPluginInputEventV1* event)
{
    auto* instance = static_cast<TriangleInstance*>(opaque);
    if (!instance || !event)
        return 0;
    if (event->kind == DRAXUL_PLUGIN_INPUT_KEY
        && event->pressed && event->logical_key == 32)
    {
        instance->paused = !instance->paused;
    }
    else if (event->kind == DRAXUL_PLUGIN_INPUT_POINTER_BUTTON
        && event->pressed && event->button == 1)
    {
        instance->direction = -instance->direction;
    }
    else
        return 0;
    if (instance->host->request_redraw)
        instance->host->request_redraw(instance->host->host_context);
    return 1;
}

DraxulPluginRenderResultV1 render_vulkan(void* opaque,
    const DraxulPluginVulkanFrameV1* frame)
{
    auto* instance = static_cast<TriangleInstance*>(opaque);
    if (!instance || !frame || !instance->visible)
        return render_result(true, DRAXUL_PLUGIN_NO_DEADLINE);
    static thread_local std::string error;
    error.clear();
    if (!instance->pipeline
        || instance->target_generation != frame->target_generation)
    {
        destroy_pipeline(*instance);
        if (!create_pipeline(*instance, *frame, error))
        {
            log(instance, DRAXUL_PLUGIN_LOG_ERROR, error);
            return render_result(false, DRAXUL_PLUGIN_NO_DEADLINE,
                error.c_str());
        }
    }
    if (instance->last_time >= 0.0 && !instance->paused)
    {
        instance->angle += instance->speed * instance->direction
            * static_cast<float>(frame->monotonic_seconds
                - instance->last_time);
    }
    instance->last_time = frame->monotonic_seconds;

    const auto command_buffer
        = static_cast<VkCommandBuffer>(frame->command_buffer);
    VkRenderPassBeginInfo begin{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    begin.renderPass = reinterpret_cast<VkRenderPass>(
        static_cast<uintptr_t>(frame->continuation_render_pass));
    begin.framebuffer = reinterpret_cast<VkFramebuffer>(
        static_cast<uintptr_t>(frame->continuation_framebuffer));
    begin.renderArea.extent = {
        static_cast<uint32_t>(frame->framebuffer_width),
        static_cast<uint32_t>(frame->framebuffer_height) };
    vkCmdBeginRenderPass(command_buffer, &begin,
        VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{};
    viewport.x = static_cast<float>(frame->viewport.x);
    viewport.y = static_cast<float>(frame->viewport.y);
    viewport.width = static_cast<float>(frame->viewport.width);
    viewport.height = static_cast<float>(frame->viewport.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{
        { std::max(0, frame->viewport.x),
            std::max(0, frame->viewport.y) },
        { static_cast<uint32_t>(std::max(0, frame->viewport.width)),
            static_cast<uint32_t>(std::max(0, frame->viewport.height)) } };
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS, instance->pipeline);
    PushConstants push;
    push.angle = instance->angle;
    push.aspect = frame->viewport.height > 0
        ? static_cast<float>(frame->viewport.width)
            / static_cast<float>(frame->viewport.height)
        : 1.0f;
    push.mode = 0;
    vkCmdPushConstants(command_buffer, instance->pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdDraw(command_buffer, 6, 1, 0, 0);
    push.mode = 1;
    vkCmdPushConstants(command_buffer, instance->pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdDraw(command_buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(command_buffer);
    return render_result(true, instance->paused
            ? DRAXUL_PLUGIN_NO_DEADLINE : kFrameDelayNs);
}

DraxulPluginRenderResultV1 render_metal(void*,
    const DraxulPluginMetalFrameV1*)
{
    return render_result(false, DRAXUL_PLUGIN_NO_DEADLINE,
        "Metal is not supported by this module build");
}

const DraxulPluginApiV1 kApi = {
    sizeof(DraxulPluginApiV1), DRAXUL_PLUGIN_ABI_V1,
    kPluginId, "Spinning Triangle", "0.1.0",
    DRAXUL_PLUGIN_BACKEND_VULKAN,
    &create_instance, &destroy_instance, &set_viewport,
    &set_visible, &set_focused, &handle_input,
    &render_vulkan, &render_metal,
};

} // namespace

extern "C" DRAXUL_PLUGIN_EXPORT const DraxulPluginApiV1*
draxul_plugin_query(uint32_t requested_abi)
{
    return requested_abi == DRAXUL_PLUGIN_ABI_V1 ? &kApi : nullptr;
}
