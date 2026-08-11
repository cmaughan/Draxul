#include <draxul/plugin_api.h>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>

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
    std::string directory;
    DraxulPluginViewportV1 viewport{};
    float speed = 1.0f;
    float angle = 0.0f;
    float direction = 1.0f;
    double last_time = -1.0;
    bool paused = false;
    bool visible = true;
    id<MTLDevice> device = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    MTLPixelFormat format = MTLPixelFormatInvalid;
};

DraxulPluginRenderResultV1 result(bool ok, uint64_t deadline,
    const char* error = nullptr)
{
    return { sizeof(DraxulPluginRenderResultV1), deadline,
        ok ? 1 : 0, error };
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

void* create_instance(const DraxulPluginCreateInfoV1* info)
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

void set_viewport(void* opaque,
    const DraxulPluginViewportV1* viewport)
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
        instance->paused = !instance->paused;
    else if (event->kind == DRAXUL_PLUGIN_INPUT_POINTER_BUTTON
        && event->pressed && event->button == 1)
        instance->direction = -instance->direction;
    else
        return 0;
    if (instance->host->request_redraw)
        instance->host->request_redraw(instance->host->host_context);
    return 1;
}

DraxulPluginRenderResultV1 render_vulkan(void*,
    const DraxulPluginVulkanFrameV1*)
{
    return result(false, DRAXUL_PLUGIN_NO_DEADLINE,
        "Vulkan is not supported by this module build");
}

DraxulPluginRenderResultV1 render_metal(void* opaque,
    const DraxulPluginMetalFrameV1* frame)
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
    if (instance->last_time >= 0.0 && !instance->paused)
    {
        instance->angle += instance->speed * instance->direction
            * static_cast<float>(frame->monotonic_seconds
                - instance->last_time);
    }
    instance->last_time = frame->monotonic_seconds;

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
    return result(true, instance->paused
            ? DRAXUL_PLUGIN_NO_DEADLINE : kFrameDelayNs);
}

const DraxulPluginApiV1 kApi = {
    sizeof(DraxulPluginApiV1), DRAXUL_PLUGIN_ABI_V1,
    kPluginId, "Spinning Triangle", "0.1.0",
    DRAXUL_PLUGIN_BACKEND_METAL,
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
