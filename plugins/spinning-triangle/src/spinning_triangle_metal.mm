#include <draxul/plugin_api.h>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

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
    std::string status;
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

void log(TriangleInstance* instance, uint32_t level,
    const std::string& message)
{
    if (instance && instance->host && instance->host->log)
    {
        instance->host->log(instance->host->host_context,
            level, message.data(), message.size());
    }
}

void notify_presentation(TriangleInstance* instance)
{
    if (instance && instance->host
        && instance->host->notify_presentation_changed)
    {
        instance->host->notify_presentation_changed(
            instance->host->host_context);
    }
}

void request_tick(TriangleInstance* instance)
{
    if (instance && instance->host && instance->host->request_tick)
        instance->host->request_tick(instance->host->host_context);
}

bool ensure_pipeline(TriangleInstance& instance,
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
        stringByAppendingPathComponent:@"spinning_triangle.metallib"];
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithFile:path error:&error];
    if (!library)
    {
        error_message = "Spinning Triangle could not load metallib: ";
        error_message += error.localizedDescription.UTF8String ?: "unknown error";
        return false;
    }
    id<MTLFunction> vertex = [library
        newFunctionWithName:@"triangle_vertex"];
    id<MTLFunction> fragment = [library
        newFunctionWithName:@"triangle_fragment"];
    MTLRenderPipelineDescriptor* descriptor
        = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = vertex;
    descriptor.fragmentFunction = fragment;
    descriptor.colorAttachments[0].pixelFormat = format;
    instance.pipeline = [device
        newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (!instance.pipeline)
    {
        error_message = "Spinning Triangle could not create pipeline: ";
        error_message += error.localizedDescription.UTF8String ?: "unknown error";
        return false;
    }
    return true;
}

void* create_instance(const DraxulPluginCreateInfoV2* info)
{
    if (!info || !info->host)
        return nullptr;
    auto* instance = new TriangleInstance;
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
    delete static_cast<TriangleInstance*>(opaque);
}

void quiesce_instance(void* opaque)
{
    auto* instance = static_cast<TriangleInstance*>(opaque);
    if (instance)
        instance->quiesced = true;
}

void set_viewport(void* opaque,
    const DraxulPluginViewportV2* viewport)
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
    request_tick(instance);
    if (instance->visible && instance->host->request_redraw)
        instance->host->request_redraw(instance->host->host_context);
    notify_presentation(instance);
}

void set_focused(void* opaque, int32_t focused)
{
    auto* instance = static_cast<TriangleInstance*>(opaque);
    if (!instance)
        return;
    instance->focused = focused != 0;
    notify_presentation(instance);
}

void toggle_pause(TriangleInstance* instance)
{
    instance->paused = !instance->paused;
    instance->last_time = -1.0;
    request_tick(instance);
    if (instance->host->request_redraw)
        instance->host->request_redraw(instance->host->host_context);
    notify_presentation(instance);
}

void reverse_direction(TriangleInstance* instance)
{
    instance->direction = -instance->direction;
    request_tick(instance);
    if (instance->host->request_redraw)
        instance->host->request_redraw(instance->host->host_context);
    notify_presentation(instance);
}

int32_t handle_input(void* opaque,
    const DraxulPluginInputEventV2* event)
{
    auto* instance = static_cast<TriangleInstance*>(opaque);
    if (!instance || !event)
        return 0;
    if (event->kind == DRAXUL_PLUGIN_INPUT_KEY
        && event->pressed && event->logical_key == 32)
        toggle_pause(instance);
    else if (event->kind == DRAXUL_PLUGIN_INPUT_POINTER_BUTTON
        && event->pressed && event->button == 1)
        reverse_direction(instance);
    else
        return 0;
    return 1;
}

DraxulPluginTickResultV2 tick(void* opaque,
    const DraxulPluginTickInfoV2* info)
{
    auto* instance = static_cast<TriangleInstance*>(opaque);
    if (!instance || !info)
        return tick_result(false, DRAXUL_PLUGIN_NO_DEADLINE,
            false, "Spinning Triangle received an invalid tick");
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
    auto* instance = static_cast<TriangleInstance*>(opaque);
    if (!instance || !frame || !instance->visible)
        return result(true, DRAXUL_PLUGIN_NO_DEADLINE);
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
            "Spinning Triangle could not create render encoder");
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
    [encoder setVertexBytes:&push length:sizeof(push) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
        vertexStart:0 vertexCount:6];
    push.mode = 1;
    [encoder setVertexBytes:&push length:sizeof(push) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
        vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    return result(true, DRAXUL_PLUGIN_NO_DEADLINE);
}

int32_t get_presentation_state(void* opaque,
    DraxulPluginPresentationStateV2* state)
{
    auto* instance = static_cast<TriangleInstance*>(opaque);
    if (!instance || !state
        || state->struct_size < sizeof(DraxulPluginPresentationStateV2))
        return 0;
    instance->status = instance->paused ? "paused" : "running";
    instance->status += instance->direction < 0.0f
        ? " counter-clockwise" : " clockwise";
    if (!instance->visible)
        instance->status += " | hidden";
    if (instance->focused)
        instance->status += " | focused";
    *state = {};
    state->struct_size = sizeof(*state);
    state->display_name = { "Spinning Triangle", 17 };
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
    auto* instance = static_cast<TriangleInstance*>(opaque);
    if (!instance || !action)
        return 0;
    const std::string_view value(action, action_length);
    if (value == "toggle_pause")
        toggle_pause(instance);
    else if (value == "reverse")
        reverse_direction(instance);
    else
        return 0;
    return 1;
}

constexpr std::string_view kActionIds[] = {
    "toggle_pause", "reverse" };
constexpr std::string_view kActionNames[] = {
    "Toggle Pause", "Reverse Direction" };

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
    .plugin_id = kPluginId,
    .display_name = "Spinning Triangle",
    .plugin_version = "0.2.0",
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
