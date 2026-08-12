#include <draxul/plugin_api.h>
#include <draxul/satview/satview_runtime.h>

#include "satview_imgui_adapter.h"

#include <nlohmann/json.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr const char* kSatViewPluginId = "dev.draxul.satview";
constexpr uint64_t kFrameDelayNs = 16'666'667;

struct PushConstants
{
    float angle = 0.0f;
    float aspect = 1.0f;
    int mode = 0;
};

class RuntimeCallbacks final
    : public draxul::satview::SatViewRuntimeCallbacks
{
public:
    void request_frame() override
    {
        if (host && host->request_redraw)
            host->request_redraw(host->host_context);
    }
    void request_quit() override {}
    void set_window_title(std::string_view) override {}

    const DraxulPluginHostApiV2* host = nullptr;
};

struct SatViewPluginInstance
{
    const DraxulPluginHostApiV2* host = nullptr;
    std::filesystem::path directory;
    DraxulPluginViewportV2 viewport{};
    float speed = 1.0f;
    float angle = 0.0f;
    float direction = 1.0f;
    double last_time = -1.0;
    bool paused = false;
    bool visible = true;
    bool focused = false;
    bool quiesced = false;
    bool remember_state = false;
    bool paths_ready = false;
    DraxulPluginPathServiceV2 paths{};
    DraxulPluginStorageServiceV2 storage{};
    bool has_storage = false;
    std::string storage_warning;
    std::string saved_config_toml;
    std::string data_directory;
    std::string status;
    RuntimeCallbacks runtime_callbacks;
    draxul::satview::plugin::ImGuiOverlayAdapter imgui_overlay;
    std::unique_ptr<draxul::satview::SatViewRuntime> runtime;
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    uint64_t target_generation = 0;
};

void log(SatViewPluginInstance* instance, uint32_t level,
    const std::string& message)
{
    if (instance && instance->host && instance->host->log)
    {
        instance->host->log(instance->host->host_context,
            level, message.data(), message.size());
    }
}

void notify_presentation(SatViewPluginInstance* instance)
{
    if (instance && instance->host
        && instance->host->notify_presentation_changed)
    {
        instance->host->notify_presentation_changed(
            instance->host->host_context);
    }
}

void request_tick(SatViewPluginInstance* instance)
{
    if (instance && instance->host && instance->host->request_tick)
        instance->host->request_tick(instance->host->host_context);
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

void destroy_pipeline(SatViewPluginInstance& instance)
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

bool create_pipeline(SatViewPluginInstance& instance,
    const DraxulPluginVulkanFrameV2& frame, std::string& error)
{
    instance.device = static_cast<VkDevice>(frame.device);
    const auto vertex_words = read_spirv(
        instance.directory / "satview_plugin.vert.spv");
    const auto fragment_words = read_spirv(
        instance.directory / "satview_plugin.frag.spv");
    if (vertex_words.empty() || fragment_words.empty())
    {
        error = "SatView shader assets are missing";
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
        error = "SatView could not create shader modules";
        return false;
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
        | VK_SHADER_STAGE_FRAGMENT_BIT;
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
        error = "SatView could not create pipeline layout";
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
    VkPipelineDepthStencilStateCreateInfo depth_stencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth_stencil.depthTestEnable = VK_FALSE;
    depth_stencil.depthWriteEnable = VK_FALSE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
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
    pipeline_info.pDepthStencilState = &depth_stencil;
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
        error = "SatView could not create graphics pipeline";
        return false;
    }
    instance.render_pass = pipeline_info.renderPass;
    instance.target_generation = frame.target_generation;
    return true;
}

DraxulPluginRenderResultV2 render_result(bool ok,
    uint64_t deadline, const char* error = nullptr)
{
    return { sizeof(DraxulPluginRenderResultV2), deadline,
        ok ? 1 : 0, error };
}

DraxulPluginTickResultV2 tick_result(bool ok, uint64_t deadline,
    bool redraw = false, const char* error = nullptr)
{
    return { sizeof(DraxulPluginTickResultV2), deadline,
        redraw ? 1 : 0, ok ? 1 : 0, error };
}

std::string service_path(DraxulPluginPathServiceV2& service,
    uint32_t kind)
{
    if (!service.get_path)
        return {};
    size_t required = 0;
    if (!service.get_path(service.service_context, kind,
            nullptr, &required)
        || required == 0)
        return {};
    std::string result(required, '\0');
    if (!service.get_path(service.service_context, kind,
            result.data(), &required))
        return {};
    result.resize(required > 0 ? required - 1 : 0);
    return result;
}

void load_saved_state(SatViewPluginInstance* instance)
{
    if (!instance->remember_state || !instance->has_storage)
        return;
    constexpr std::string_view key = "state";
    size_t required = 0;
    const uint32_t first = instance->storage.read_json(
        instance->storage.service_context, DRAXUL_PLUGIN_STORAGE_PANE,
        key.data(), key.size(), nullptr, &required);
    if (first == DRAXUL_PLUGIN_STORAGE_NOT_FOUND)
        return;
    if (first != DRAXUL_PLUGIN_STORAGE_OK || required == 0)
    {
        instance->storage_warning = first == DRAXUL_PLUGIN_STORAGE_INVALID_JSON
            ? "saved state is corrupt" : "saved state could not be read";
        return;
    }
    std::string value(required, '\0');
    const uint32_t second = instance->storage.read_json(
        instance->storage.service_context, DRAXUL_PLUGIN_STORAGE_PANE,
        key.data(), key.size(), value.data(), &required);
    if (second != DRAXUL_PLUGIN_STORAGE_OK)
    {
        instance->storage_warning = "saved state could not be read";
        return;
    }
    value.resize(required > 0 ? required - 1 : 0);
    try
    {
        const auto state = nlohmann::json::parse(value);
        instance->paused = state.value("paused", instance->paused);
        const float direction = state.value("direction", instance->direction);
        instance->direction = direction < 0.0f ? -1.0f : 1.0f;
        instance->saved_config_toml = state.value(
            "satview_config_toml", std::string{});
    }
    catch (...)
    {
        instance->storage_warning = "saved state is corrupt";
    }
}

void save_state(SatViewPluginInstance* instance)
{
    if (!instance->remember_state || !instance->has_storage)
        return;
    constexpr std::string_view key = "state";
    nlohmann::json state{
        { "paused", instance->paused },
        { "direction", instance->direction },
    };
    if (instance->runtime)
    {
        state["satview_config_toml"]
            = draxul::satview::serialize_satview_config_toml(
                instance->runtime->current_config());
    }
    const std::string value = state.dump();
    const uint32_t result = instance->storage.write_json(
        instance->storage.service_context, DRAXUL_PLUGIN_STORAGE_PANE,
        key.data(), key.size(), value.data(), value.size());
    if (result == DRAXUL_PLUGIN_STORAGE_OK)
        instance->storage_warning.clear();
    else
        instance->storage_warning = "saved state could not be written";
}

void* create_instance(const DraxulPluginCreateInfoV2* info)
{
    if (!info || !info->host)
        return nullptr;
    auto* instance = new SatViewPluginInstance;
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
        instance->remember_state = config.value("remember_state", true);
    }
    catch (...)
    {
        delete instance;
        return nullptr;
    }
    if (instance->host->query_service)
    {
        instance->paths.struct_size = sizeof(instance->paths);
        instance->paths.service_version = DRAXUL_PLUGIN_PATH_SERVICE_VERSION;
        instance->paths_ready = instance->host->query_service(
            instance->host->host_context, DRAXUL_PLUGIN_PATH_SERVICE_ID,
            std::strlen(DRAXUL_PLUGIN_PATH_SERVICE_ID),
            DRAXUL_PLUGIN_PATH_SERVICE_VERSION,
            &instance->paths, sizeof(instance->paths)) != 0;
        instance->storage.struct_size = sizeof(instance->storage);
        instance->storage.service_version = DRAXUL_PLUGIN_STORAGE_SERVICE_VERSION;
        instance->has_storage = instance->host->query_service(
            instance->host->host_context, DRAXUL_PLUGIN_STORAGE_SERVICE_ID,
            std::strlen(DRAXUL_PLUGIN_STORAGE_SERVICE_ID),
            DRAXUL_PLUGIN_STORAGE_SERVICE_VERSION,
            &instance->storage, sizeof(instance->storage)) != 0;
        instance->imgui_overlay.discover(*instance->host);
    }
    if (instance->paths_ready)
        instance->data_directory = service_path(
            instance->paths, DRAXUL_PLUGIN_PATH_DATA);
    load_saved_state(instance);
    instance->runtime_callbacks.host = instance->host;
    instance->runtime = std::make_unique<draxul::satview::SatViewRuntime>();
    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.show_host_ui_panels
        = instance->imgui_overlay.available();
    context.initial_viewport.pixel_pos = {
        info->initial_viewport.x, info->initial_viewport.y };
    context.initial_viewport.pixel_size = {
        info->initial_viewport.width, info->initial_viewport.height };
    context.initial_viewport.pixel_scale = info->initial_viewport.pixel_scale;
    const std::filesystem::path cache_root = instance->paths_ready
        ? std::filesystem::u8path(service_path(
              instance->paths, DRAXUL_PLUGIN_PATH_CACHE))
        : std::filesystem::path{};
    if (!instance->runtime->initialize(context,
            instance->runtime_callbacks,
            instance->directory / "assets", cache_root))
    {
        delete instance;
        return nullptr;
    }
    if (!instance->saved_config_toml.empty())
    {
        if (const auto config = draxul::satview::parse_satview_config_toml(
                instance->saved_config_toml))
            instance->runtime->apply_config(*config);
        else
            instance->storage_warning = "saved SatView preferences are corrupt";
    }
    if (instance->imgui_overlay.available())
        instance->runtime->attach_imgui_host(instance->imgui_overlay);
    return instance;
}

void destroy_instance(void* opaque)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (!instance)
        return;
    if (instance->runtime)
        instance->runtime->shutdown();
    destroy_pipeline(*instance);
    delete instance;
}

void quiesce_instance(void* opaque)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (instance)
    {
        if (instance->runtime)
            instance->runtime->shutdown();
        save_state(instance);
        instance->quiesced = true;
    }
}

void set_viewport(void* opaque, const DraxulPluginViewportV2* viewport)
{
    if (!opaque || !viewport)
        return;
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    instance->viewport = *viewport;
    if (instance->runtime)
    {
        draxul::HostViewport value;
        value.pixel_pos = { viewport->x, viewport->y };
        value.pixel_size = { viewport->width, viewport->height };
        value.pixel_scale = viewport->pixel_scale;
        instance->runtime->set_viewport(value);
    }
}

void set_visible(void* opaque, int32_t visible)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (!instance)
        return;
    instance->visible = visible != 0;
    instance->last_time = -1.0;
    request_tick(instance);
    if (instance->visible && instance->host->request_redraw)
        instance->host->request_redraw(instance->host->host_context);
    notify_presentation(instance);
}

void set_focused(void* opaque, int32_t focused)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (!instance)
        return;
    instance->focused = focused != 0;
    notify_presentation(instance);
}

void toggle_pause(SatViewPluginInstance* instance,
    bool runtime_already_toggled = false)
{
    instance->paused = !instance->paused;
    if (instance->runtime && !runtime_already_toggled)
        instance->runtime->dispatch_action("satview_toggle_pause");
    instance->last_time = -1.0;
    save_state(instance);
    request_tick(instance);
    if (instance->host->request_redraw)
        instance->host->request_redraw(instance->host->host_context);
    notify_presentation(instance);
}

int32_t handle_input(void* opaque,
    const DraxulPluginInputEventV2* event)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (!instance || !event)
        return 0;
    if (instance->runtime)
    {
        switch (event->kind)
        {
        case DRAXUL_PLUGIN_INPUT_KEY:
            instance->runtime->on_key({
                static_cast<int>(event->physical_key), event->logical_key,
                static_cast<draxul::ModifierFlags>(event->modifiers),
                event->pressed != 0 });
            break;
        case DRAXUL_PLUGIN_INPUT_TEXT:
            instance->runtime->on_text_input({ std::string(
                event->text_utf8 ? event->text_utf8 : "",
                event->text_utf8 ? event->text_length : 0) });
            break;
        case DRAXUL_PLUGIN_INPUT_POINTER_BUTTON:
            instance->runtime->on_mouse_button({ event->button,
                event->pressed != 0,
                static_cast<draxul::ModifierFlags>(event->modifiers),
                { event->x + instance->viewport.x,
                    event->y + instance->viewport.y }, event->clicks });
            break;
        case DRAXUL_PLUGIN_INPUT_POINTER_MOVE:
            instance->runtime->on_mouse_move({
                static_cast<draxul::ModifierFlags>(event->modifiers),
                { event->x + instance->viewport.x,
                    event->y + instance->viewport.y },
                { event->delta_x, event->delta_y }, event->buttons });
            break;
        case DRAXUL_PLUGIN_INPUT_WHEEL:
            instance->runtime->on_mouse_wheel({
                { event->delta_x, event->delta_y },
                static_cast<draxul::ModifierFlags>(event->modifiers),
                { event->x + instance->viewport.x,
                    event->y + instance->viewport.y } });
            break;
        case DRAXUL_PLUGIN_INPUT_FOCUS:
            if (!event->pressed)
                instance->runtime->on_focus_lost();
            break;
        default:
            break;
        }
    }
    if (event->kind == DRAXUL_PLUGIN_INPUT_KEY
        && event->pressed && event->logical_key == 32)
    {
        toggle_pause(instance, true);
    }
    return 1;
}

DraxulPluginTickResultV2 tick(void* opaque,
    const DraxulPluginTickInfoV2* info)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (!instance || !info)
        return tick_result(false, DRAXUL_PLUGIN_NO_DEADLINE,
            false, "SatView received an invalid tick");
    if (instance->runtime)
    {
        instance->imgui_overlay.synchronize_font(*instance->runtime);
        instance->runtime->pump();
    }
    if (instance->quiesced || !instance->visible || !info->visible
        || instance->paused)
    {
        instance->last_time = -1.0;
        return tick_result(true, DRAXUL_PLUGIN_NO_DEADLINE);
    }
    if (instance->last_time >= 0.0)
    {
        const double elapsed = std::clamp(
            info->monotonic_seconds - instance->last_time, 0.0, 0.1);
        instance->angle += instance->speed * instance->direction
            * static_cast<float>(elapsed);
    }
    instance->last_time = info->monotonic_seconds;
    return tick_result(true, kFrameDelayNs, true);
}

DraxulPluginRenderResultV2 render_vulkan(void* opaque,
    const DraxulPluginVulkanFrameV2* frame)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (!instance || !frame || !instance->visible)
        return render_result(true, DRAXUL_PLUGIN_NO_DEADLINE);
    draxul::satview::plugin::DeferredOverlaySink sink(
        &instance->imgui_overlay);
    if (instance->runtime)
        instance->runtime->draw(sink);
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
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(push), &push);
    vkCmdDraw(command_buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(command_buffer);
    sink.render();
    return render_result(true, DRAXUL_PLUGIN_NO_DEADLINE);
}

DraxulPluginRenderResultV2 render_metal(void*,
    const DraxulPluginMetalFrameV2*)
{
    return render_result(false, DRAXUL_PLUGIN_NO_DEADLINE,
        "Metal is not supported by this module build");
}

int32_t get_presentation_state(void* opaque,
    DraxulPluginPresentationStateV2* state)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (!instance || !state
        || state->struct_size < sizeof(DraxulPluginPresentationStateV2))
        return 0;
    instance->status = instance->runtime
        ? instance->runtime->status_text() : "satview loading";
    if (!instance->visible)
        instance->status += " | hidden";
    if (instance->focused)
        instance->status += " | focused";
    if (instance->remember_state)
        instance->status += instance->has_storage
            ? " | remembered" : " | storage unavailable";
    if (instance->paths_ready && !instance->data_directory.empty())
        instance->status += " | paths ready";
    if (!instance->storage_warning.empty())
        instance->status += " | " + instance->storage_warning;
    *state = {};
    state->struct_size = sizeof(*state);
    state->display_name = { "SatView", 7 };
    state->status_text = {
        instance->status.data(), instance->status.size() };
    state->background_red = 0.04f;
    state->background_green = 0.05f;
    state->background_blue = 0.08f;
    state->background_alpha = 1.0f;
    state->content_ready = instance->quiesced ? 0 : 1;
    state->mouse_cursor = DRAXUL_PLUGIN_CURSOR_POINTER;
    return 1;
}

int32_t dispatch_action(void* opaque, const char* action,
    size_t action_length)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (!instance || !action)
        return 0;
    const std::string_view value(action, action_length);
    if (value == "satview_toggle_pause")
        toggle_pause(instance);
    else if (instance->runtime
        && instance->runtime->dispatch_action(value))
    {
        save_state(instance);
        notify_presentation(instance);
    }
    else
        return 0;
    return 1;
}

constexpr std::string_view kActionIds[] = {
    "toggle_ui_panels", "satview_toggle_pause", "satview_time_slower",
    "satview_time_faster", "satview_reset_camera",
    "satview_refresh_catalog", "satview_clear_selection" };
constexpr std::string_view kActionNames[] = {
    "Toggle Control Panels", "Toggle Pause", "Slower Time", "Faster Time",
    "Reset Camera", "Refresh Catalog", "Clear Selection" };

size_t action_count(void*)
{
    return std::size(kActionIds);
}

int32_t action_at(void*, size_t index,
    DraxulPluginStringViewV2* action_id,
    DraxulPluginStringViewV2* display_name)
{
    if (!action_id || !display_name || index >= std::size(kActionIds))
        return 0;
    *action_id = { kActionIds[index].data(), kActionIds[index].size() };
    *display_name = {
        kActionNames[index].data(), kActionNames[index].size() };
    return 1;
}

int32_t query_extension(void*, const char* extension_id,
    size_t extension_id_length, uint32_t requested_version,
    void* extension_table, size_t extension_table_size)
{
    if (!extension_id || !extension_table
        || std::string_view(extension_id, extension_id_length)
            != DRAXUL_PLUGIN_PRESENTATION_EXTENSION_ID
        || requested_version != DRAXUL_PLUGIN_PRESENTATION_EXTENSION_VERSION
        || extension_table_size < sizeof(DraxulPluginPresentationExtensionV2))
        return 0;
    auto* extension = static_cast<DraxulPluginPresentationExtensionV2*>(
        extension_table);
    *extension = {
        sizeof(*extension), DRAXUL_PLUGIN_PRESENTATION_EXTENSION_VERSION,
        &get_presentation_state, &dispatch_action,
        &action_count, &action_at };
    return 1;
}

const DraxulPluginApiV2 kApi = {
    .struct_size = sizeof(DraxulPluginApiV2),
    .abi_version = DRAXUL_PLUGIN_ABI_VERSION,
    .plugin_id = kSatViewPluginId,
    .display_name = "SatView",
    .plugin_version = "0.1.0",
    .supported_backends = DRAXUL_PLUGIN_BACKEND_VULKAN,
    .create_instance = &create_instance,
    .quiesce_instance = &quiesce_instance,
    .destroy_instance = &destroy_instance,
    .set_viewport = &set_viewport,
    .set_visible = &set_visible,
    .set_focused = &set_focused,
    .handle_input = &handle_input,
    .tick = &tick,
    .render_vulkan = &render_vulkan,
    .render_metal = &render_metal,
    .query_extension = &query_extension,
};

} // namespace

extern "C" DRAXUL_PLUGIN_EXPORT const DraxulPluginApiV2*
draxul_plugin_query_v2(uint32_t requested_abi)
{
    return requested_abi == DRAXUL_PLUGIN_ABI_VERSION ? &kApi : nullptr;
}
