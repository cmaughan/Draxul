#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define DRAXUL_PLUGIN_EXPORT __declspec(dllexport)
#else
#define DRAXUL_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define DRAXUL_PLUGIN_ABI_V1 1u
#define DRAXUL_PLUGIN_QUERY_SYMBOL "draxul_plugin_query"
#define DRAXUL_PLUGIN_NO_DEADLINE UINT64_MAX

typedef enum DraxulPluginBackendV1
{
    DRAXUL_PLUGIN_BACKEND_NONE = 0,
    DRAXUL_PLUGIN_BACKEND_VULKAN = 1u << 0,
    DRAXUL_PLUGIN_BACKEND_METAL = 1u << 1,
} DraxulPluginBackendV1;

typedef enum DraxulPluginLogLevelV1
{
    DRAXUL_PLUGIN_LOG_DEBUG = 0,
    DRAXUL_PLUGIN_LOG_INFO = 1,
    DRAXUL_PLUGIN_LOG_WARNING = 2,
    DRAXUL_PLUGIN_LOG_ERROR = 3,
} DraxulPluginLogLevelV1;

typedef enum DraxulPluginInputKindV1
{
    DRAXUL_PLUGIN_INPUT_KEY = 1,
    DRAXUL_PLUGIN_INPUT_TEXT = 2,
    DRAXUL_PLUGIN_INPUT_COMPOSITION = 3,
    DRAXUL_PLUGIN_INPUT_POINTER_BUTTON = 4,
    DRAXUL_PLUGIN_INPUT_POINTER_MOVE = 5,
    DRAXUL_PLUGIN_INPUT_WHEEL = 6,
    DRAXUL_PLUGIN_INPUT_FOCUS = 7,
} DraxulPluginInputKindV1;

typedef struct DraxulPluginViewportV1
{
    uint32_t struct_size;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    float pixel_scale;
    float display_ppi;
} DraxulPluginViewportV1;

typedef struct DraxulPluginInputEventV1
{
    uint32_t struct_size;
    uint32_t kind;
    uint32_t modifiers;
    uint32_t physical_key;
    int32_t logical_key;
    int32_t pressed;
    int32_t button;
    int32_t clicks;
    int32_t x;
    int32_t y;
    float delta_x;
    float delta_y;
    uint32_t buttons;
    const char* text_utf8;
    size_t text_length;
    int32_t composition_start;
    int32_t composition_length;
} DraxulPluginInputEventV1;

typedef struct DraxulPluginHostApiV1
{
    uint32_t struct_size;
    uint32_t abi_version;
    void* host_context;
    void (*request_redraw)(void* host_context);
    void (*log)(void* host_context, uint32_t level, const char* message, size_t message_length);
} DraxulPluginHostApiV1;

typedef struct DraxulPluginCreateInfoV1
{
    uint32_t struct_size;
    const DraxulPluginHostApiV1* host;
    const char* plugin_id;
    const char* plugin_directory_utf8;
    const char* config_json;
    size_t config_json_length;
    DraxulPluginViewportV1 initial_viewport;
} DraxulPluginCreateInfoV1;

typedef struct DraxulPluginVulkanFrameV1
{
    uint32_t struct_size;
    void* instance;
    void* physical_device;
    void* device;
    void* command_buffer;
    uint64_t target_image;
    uint64_t target_image_view;
    uint64_t target_format;
    uint64_t depth_format;
    uint64_t continuation_render_pass;
    uint64_t continuation_framebuffer;
    uint32_t frame_index;
    uint32_t buffered_frame_count;
    uint64_t target_generation;
    int32_t framebuffer_width;
    int32_t framebuffer_height;
    DraxulPluginViewportV1 viewport;
    double monotonic_seconds;
} DraxulPluginVulkanFrameV1;

typedef struct DraxulPluginMetalFrameV1
{
    uint32_t struct_size;
    void* device;
    void* command_buffer;
    void* drawable_texture;
    void* continuation_render_pass_descriptor;
    uint64_t target_generation;
    uint32_t frame_index;
    uint32_t buffered_frame_count;
    int32_t framebuffer_width;
    int32_t framebuffer_height;
    DraxulPluginViewportV1 viewport;
    double monotonic_seconds;
} DraxulPluginMetalFrameV1;

typedef struct DraxulPluginRenderResultV1
{
    uint32_t struct_size;
    uint64_t next_frame_delay_ns;
    int32_t ok;
    const char* error_message;
} DraxulPluginRenderResultV1;

typedef struct DraxulPluginApiV1
{
    uint32_t struct_size;
    uint32_t abi_version;
    const char* plugin_id;
    const char* display_name;
    const char* plugin_version;
    uint32_t supported_backends;
    void* (*create_instance)(const DraxulPluginCreateInfoV1* create_info);
    void (*destroy_instance)(void* instance);
    void (*set_viewport)(void* instance, const DraxulPluginViewportV1* viewport);
    void (*set_visible)(void* instance, int32_t visible);
    void (*set_focused)(void* instance, int32_t focused);
    int32_t (*handle_input)(void* instance, const DraxulPluginInputEventV1* event);
    DraxulPluginRenderResultV1 (*render_vulkan)(void* instance, const DraxulPluginVulkanFrameV1* frame);
    DraxulPluginRenderResultV1 (*render_metal)(void* instance, const DraxulPluginMetalFrameV1* frame);
} DraxulPluginApiV1;

typedef const DraxulPluginApiV1* (*DraxulPluginQueryFn)(uint32_t requested_abi);

DRAXUL_PLUGIN_EXPORT const DraxulPluginApiV1* draxul_plugin_query(uint32_t requested_abi);

#ifdef __cplusplus
}
#endif
