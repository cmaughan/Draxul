#include <draxul/plugin_api.h>
#include <draxul/satview/satview_runtime.h>

#include "satview_imgui_adapter.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

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
    std::string directory;
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
    id<MTLDevice> device = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    MTLPixelFormat format = MTLPixelFormatInvalid;
};

DraxulPluginRenderResultV2 result(bool ok, uint64_t deadline,
    const char* error = nullptr)
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

bool ensure_pipeline(SatViewPluginInstance& instance,
    id<MTLDevice> device, MTLPixelFormat format,
    std::string& error_message)
{
    if (instance.pipeline && instance.device == device
        && instance.format == format)
        return true;
    instance.pipeline = nil;
    instance.device = device;
    instance.format = format;
    NSString* directory = [NSString
        stringWithUTF8String:instance.directory.c_str()];
    NSString* path = [directory
        stringByAppendingPathComponent:@"satview_plugin.metallib"];
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithFile:path error:&error];
    if (!library)
    {
        error_message = "SatView could not load metallib: ";
        error_message += error.localizedDescription.UTF8String ?: "unknown error";
        return false;
    }
    id<MTLFunction> vertex = [library
        newFunctionWithName:@"satview_vertex"];
    id<MTLFunction> fragment = [library
        newFunctionWithName:@"satview_fragment"];
    MTLRenderPipelineDescriptor* descriptor
        = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = vertex;
    descriptor.fragmentFunction = fragment;
    descriptor.colorAttachments[0].pixelFormat = format;
    instance.pipeline = [device
        newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (!instance.pipeline)
    {
        error_message = "SatView could not create pipeline: ";
        error_message += error.localizedDescription.UTF8String ?: "unknown error";
        return false;
    }
    return true;
}

void* create_instance(const DraxulPluginCreateInfoV2* info)
{
    if (!info || !info->host)
        return nullptr;
    auto* instance = new SatViewPluginInstance;
    instance->host = info->host;
    instance->directory = info->plugin_directory_utf8
        ? info->plugin_directory_utf8 : "";
    instance->viewport = info->initial_viewport;
    try
    {
        const char* begin = info->config_json
            ? info->config_json : "{}";
        const char* end = info->config_json
            ? info->config_json + info->config_json_length
            : begin + 2;
        const auto config = nlohmann::json::parse(begin, end);
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
    context.launch_options.kind = draxul::HostKind::SatView;
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
            std::filesystem::u8path(instance->directory) / "assets",
            cache_root))
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
    if (instance && instance->runtime)
        instance->runtime->shutdown();
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

void set_viewport(void* opaque,
    const DraxulPluginViewportV2* viewport)
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
        toggle_pause(instance, true);
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

DraxulPluginRenderResultV2 render_vulkan(void*,
    const DraxulPluginVulkanFrameV2*)
{
    return result(false, DRAXUL_PLUGIN_NO_DEADLINE,
        "Vulkan is not supported by this module build");
}

DraxulPluginRenderResultV2 render_metal(void* opaque,
    const DraxulPluginMetalFrameV2* frame)
{
    auto* instance = static_cast<SatViewPluginInstance*>(opaque);
    if (!instance || !frame || !instance->visible)
        return result(true, DRAXUL_PLUGIN_NO_DEADLINE);
    draxul::satview::plugin::DeferredOverlaySink sink(
        &instance->imgui_overlay);
    if (instance->runtime)
        instance->runtime->draw(sink);
    id<MTLDevice> device
        = (__bridge id<MTLDevice>)frame->device;
    id<MTLCommandBuffer> command_buffer
        = (__bridge id<MTLCommandBuffer>)frame->command_buffer;
    id<MTLTexture> texture
        = (__bridge id<MTLTexture>)frame->drawable_texture;
    MTLRenderPassDescriptor* descriptor
        = (__bridge MTLRenderPassDescriptor*)
            frame->continuation_render_pass_descriptor;
    static thread_local std::string error;
    error.clear();
    if (!ensure_pipeline(*instance, device,
            texture.pixelFormat, error))
    {
        log(instance, DRAXUL_PLUGIN_LOG_ERROR, error);
        return result(false, DRAXUL_PLUGIN_NO_DEADLINE,
            error.c_str());
    }
    id<MTLRenderCommandEncoder> encoder
        = [command_buffer renderCommandEncoderWithDescriptor:descriptor];
    if (!encoder)
        return result(false, DRAXUL_PLUGIN_NO_DEADLINE,
            "SatView could not create render encoder");
    const auto& viewport = frame->viewport;
    [encoder setViewport:MTLViewport{
        static_cast<double>(viewport.x),
        static_cast<double>(viewport.y),
        static_cast<double>(std::max(0, viewport.width)),
        static_cast<double>(std::max(0, viewport.height)), 0.0, 1.0 }];
    [encoder setScissorRect:MTLScissorRect{
        static_cast<NSUInteger>(std::max(0, viewport.x)),
        static_cast<NSUInteger>(std::max(0, viewport.y)),
        static_cast<NSUInteger>(std::max(0, viewport.width)),
        static_cast<NSUInteger>(std::max(0, viewport.height)) }];
    [encoder setRenderPipelineState:instance->pipeline];
    PushConstants push;
    push.angle = instance->angle;
    push.aspect = viewport.height > 0
        ? static_cast<float>(viewport.width)
            / static_cast<float>(viewport.height)
        : 1.0f;
    push.mode = 0;
    [encoder setFragmentBytes:&push length:sizeof(push) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
        vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    sink.render();
    return result(true, DRAXUL_PLUGIN_NO_DEADLINE);
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
    .supported_backends = DRAXUL_PLUGIN_BACKEND_METAL,
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
