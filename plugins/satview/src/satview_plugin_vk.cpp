#include <draxul/plugin_api.h>
#include <draxul/satview/satview_scene_pass.h>
#include <draxul/satview/satview_runtime.h>
#include <draxul/satview/satview_texture_assets.h>
#include <draxul/vulkan/vk_render_context.h>

#include "satview_imgui_adapter.h"

#include <nlohmann/json.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr const char* kSatViewPluginId = "dev.draxul.satview";
constexpr uint64_t kFrameDelayNs = 16'666'667;

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
    std::unique_ptr<draxul::plugin_support::GpuImGuiHost> imgui_overlay;
    draxul::plugin_support::UiStyleClient ui_style;
    std::unique_ptr<draxul::satview::SatViewRuntime> runtime;
    VmaAllocator allocator = VK_NULL_HANDLE;
};

bool ensure_allocator(SatViewPluginInstance& instance,
    const DraxulPluginVulkanFrameV2& frame, std::string& error);

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

void synchronize_ui_style(SatViewPluginInstance& instance)
{
    if (instance.runtime)
    {
        if (const auto font = instance.ui_style.poll())
            instance.runtime->set_imgui_font(font->path, font->size_pixels);
    }
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
    draxul::satview::set_satview_asset_root(instance->directory / "assets");
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
        instance->ui_style.discover(*instance->host);
    }
    instance->imgui_overlay
        = draxul::plugin_support::create_gpu_imgui_host();
    if (instance->paths_ready)
        instance->data_directory = service_path(
            instance->paths, DRAXUL_PLUGIN_PATH_DATA);
    load_saved_state(instance);
    instance->runtime_callbacks.host = instance->host;
    instance->runtime = std::make_unique<draxul::satview::SatViewRuntime>();
    draxul::PluginRuntimeContext context;
    context.launch_options.show_ui_panels
        = instance->imgui_overlay != nullptr;
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
    if (instance->paused)
        instance->runtime->dispatch_action("satview_toggle_pause");
    if (!instance->saved_config_toml.empty())
    {
        if (const auto config = draxul::satview::parse_satview_config_toml(
                instance->saved_config_toml))
            instance->runtime->apply_config(*config);
        else
            instance->storage_warning = "saved SatView preferences are corrupt";
    }
    if (instance->imgui_overlay)
        instance->runtime->attach_imgui_host(*instance->imgui_overlay);
    synchronize_ui_style(*instance);
    return instance;
}

void destroy_instance(void* opaque)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (!instance)
        return;
    if (instance->runtime)
        instance->runtime->shutdown();
    if (instance->allocator)
        vmaDestroyAllocator(instance->allocator);
    delete instance;
}

void quiesce_instance(void* opaque)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (instance)
    {
        if (instance->runtime)
            instance->runtime->quiesce();
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
        draxul::PluginRuntimeViewport value;
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
        synchronize_ui_style(*instance);
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
        instance->imgui_overlay.get());
    if (instance->imgui_overlay)
        instance->imgui_overlay->set_vulkan_frame(frame);
    if (instance->runtime)
        instance->runtime->draw(sink);
    static thread_local std::string error;
    error.clear();
    if (!sink.scene_pass())
        return render_result(false, DRAXUL_PLUGIN_NO_DEADLINE,
            "SatView runtime did not provide its scene");
    if (!ensure_allocator(*instance, *frame, error))
    {
        log(instance, DRAXUL_PLUGIN_LOG_ERROR, error);
        return render_result(false, DRAXUL_PLUGIN_NO_DEADLINE,
            error.c_str());
    }
    const auto command_buffer
        = static_cast<VkCommandBuffer>(frame->command_buffer);
    const auto render_pass = reinterpret_cast<VkRenderPass>(
        static_cast<uintptr_t>(frame->continuation_render_pass));
    draxul::VkRenderContext scene_context(command_buffer,
        static_cast<VkPhysicalDevice>(frame->physical_device),
        static_cast<VkDevice>(frame->device), instance->allocator,
        render_pass, frame->frame_index, frame->buffered_frame_count,
        frame->framebuffer_width, frame->framebuffer_height,
        sink.scene_x(), sink.scene_y(),
        std::max(1, sink.scene_width()), std::max(1, sink.scene_height()),
        reinterpret_cast<VkImage>(static_cast<uintptr_t>(frame->target_image)),
        reinterpret_cast<VkImageView>(static_cast<uintptr_t>(frame->target_image_view)),
        static_cast<VkFormat>(frame->target_format),
        static_cast<VkQueue>(frame->graphics_queue),
        frame->graphics_queue_family,
        static_cast<VkInstance>(frame->instance), render_pass,
        reinterpret_cast<VkFramebuffer>(static_cast<uintptr_t>(frame->continuation_framebuffer)),
        static_cast<VkFormat>(frame->depth_format), frame->target_generation);
    sink.scene_pass()->record_prepass(scene_context);
    VkRenderPassBeginInfo begin{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    begin.renderPass = render_pass;
    begin.framebuffer = reinterpret_cast<VkFramebuffer>(
        static_cast<uintptr_t>(frame->continuation_framebuffer));
    begin.renderArea.extent = {
        static_cast<uint32_t>(frame->framebuffer_width),
        static_cast<uint32_t>(frame->framebuffer_height) };
    vkCmdBeginRenderPass(command_buffer, &begin,
        VK_SUBPASS_CONTENTS_INLINE);
    sink.scene_pass()->record(scene_context);
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

bool ensure_allocator(SatViewPluginInstance& instance,
    const DraxulPluginVulkanFrameV2& frame, std::string& error)
{
    if (instance.allocator != VK_NULL_HANDLE)
        return true;
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo info{};
    info.instance = static_cast<VkInstance>(frame.instance);
    info.physicalDevice = static_cast<VkPhysicalDevice>(frame.physical_device);
    info.device = static_cast<VkDevice>(frame.device);
    info.pVulkanFunctions = &functions;
    info.vulkanApiVersion = VK_API_VERSION_1_2;
    const VkResult result = vmaCreateAllocator(&info, &instance.allocator);
    if (result != VK_SUCCESS)
    {
        error = "SatView could not create its Vulkan allocator ("
            + std::to_string(result) + ")";
        return false;
    }
    return true;
}

} // namespace

extern "C" DRAXUL_PLUGIN_EXPORT const DraxulPluginApiV2*
draxul_plugin_query_v2(uint32_t requested_abi)
{
    return requested_abi == DRAXUL_PLUGIN_ABI_VERSION ? &kApi : nullptr;
}
