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

// Draxul is pre-release and accepts only this ABI. Changes to the native
// contract update the host, bundled plugins, fixtures, and manifest together;
// there is deliberately no v1 compatibility loader.
#define DRAXUL_PLUGIN_ABI_VERSION 2u
#define DRAXUL_PLUGIN_QUERY_SYMBOL "draxul_plugin_query_v2"
#define DRAXUL_PLUGIN_NO_DEADLINE UINT64_MAX

#define DRAXUL_PLUGIN_PRESENTATION_EXTENSION_ID "draxul.presentation"
#define DRAXUL_PLUGIN_PRESENTATION_EXTENSION_VERSION 1u
#define DRAXUL_PLUGIN_PATH_SERVICE_ID "draxul.paths"
#define DRAXUL_PLUGIN_PATH_SERVICE_VERSION 1u
#define DRAXUL_PLUGIN_STORAGE_SERVICE_ID "draxul.storage"
#define DRAXUL_PLUGIN_STORAGE_SERVICE_VERSION 1u
#define DRAXUL_PLUGIN_MAX_STORAGE_JSON_BYTES (1024u * 1024u)

typedef enum DraxulPluginBackendV2
{
    DRAXUL_PLUGIN_BACKEND_NONE = 0,
    DRAXUL_PLUGIN_BACKEND_VULKAN = 1u << 0,
    DRAXUL_PLUGIN_BACKEND_METAL = 1u << 1,
} DraxulPluginBackendV2;

typedef enum DraxulPluginLogLevelV2
{
    DRAXUL_PLUGIN_LOG_DEBUG = 0,
    DRAXUL_PLUGIN_LOG_INFO = 1,
    DRAXUL_PLUGIN_LOG_WARNING = 2,
    DRAXUL_PLUGIN_LOG_ERROR = 3,
} DraxulPluginLogLevelV2;

typedef enum DraxulPluginInputKindV2
{
    DRAXUL_PLUGIN_INPUT_KEY = 1,
    DRAXUL_PLUGIN_INPUT_TEXT = 2,
    DRAXUL_PLUGIN_INPUT_COMPOSITION = 3,
    DRAXUL_PLUGIN_INPUT_POINTER_BUTTON = 4,
    DRAXUL_PLUGIN_INPUT_POINTER_MOVE = 5,
    DRAXUL_PLUGIN_INPUT_WHEEL = 6,
    DRAXUL_PLUGIN_INPUT_FOCUS = 7,
} DraxulPluginInputKindV2;

typedef enum DraxulPluginMouseCursorV2
{
    DRAXUL_PLUGIN_CURSOR_UNSPECIFIED = 0,
    DRAXUL_PLUGIN_CURSOR_DEFAULT = 1,
    DRAXUL_PLUGIN_CURSOR_RESIZE_LEFT_RIGHT = 2,
    DRAXUL_PLUGIN_CURSOR_RESIZE_UP_DOWN = 3,
    DRAXUL_PLUGIN_CURSOR_POINTER = 4,
} DraxulPluginMouseCursorV2;

typedef struct DraxulPluginStringViewV2
{
    const char* data;
    size_t length;
} DraxulPluginStringViewV2;

typedef enum DraxulPluginPathKindV2
{
    DRAXUL_PLUGIN_PATH_RESOURCES = 1,
    DRAXUL_PLUGIN_PATH_CONFIG = 2,
    DRAXUL_PLUGIN_PATH_DATA = 3,
    DRAXUL_PLUGIN_PATH_CACHE = 4,
    DRAXUL_PLUGIN_PATH_TEMPORARY = 5,
} DraxulPluginPathKindV2;

typedef enum DraxulPluginStorageScopeV2
{
    DRAXUL_PLUGIN_STORAGE_PLUGIN = 1,
    DRAXUL_PLUGIN_STORAGE_PANE = 2,
} DraxulPluginStorageScopeV2;

typedef enum DraxulPluginStorageResultV2
{
    DRAXUL_PLUGIN_STORAGE_OK = 0,
    DRAXUL_PLUGIN_STORAGE_NOT_FOUND = 1,
    DRAXUL_PLUGIN_STORAGE_INVALID_KEY = 2,
    DRAXUL_PLUGIN_STORAGE_TOO_LARGE = 3,
    DRAXUL_PLUGIN_STORAGE_INVALID_JSON = 4,
    DRAXUL_PLUGIN_STORAGE_WRONG_THREAD = 5,
    DRAXUL_PLUGIN_STORAGE_IO_ERROR = 6,
    DRAXUL_PLUGIN_STORAGE_BUFFER_TOO_SMALL = 7,
    DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE = 8,
} DraxulPluginStorageResultV2;

// get_path uses a conventional two-call buffer contract. With buffer=null,
// *in_out_size receives the required byte count including the trailing NUL.
typedef struct DraxulPluginPathServiceV2
{
    uint32_t struct_size;
    uint32_t service_version;
    void* service_context;
    int32_t (*get_path)(void* service_context, uint32_t path_kind,
        char* buffer, size_t* in_out_size);
} DraxulPluginPathServiceV2;

// Storage values are complete UTF-8 JSON documents. Keys are bounded simple
// names, not paths. Calls are main-thread-only and writes replace atomically.
typedef struct DraxulPluginStorageServiceV2
{
    uint32_t struct_size;
    uint32_t service_version;
    void* service_context;
    uint32_t (*read_json)(void* service_context, uint32_t scope,
        const char* key, size_t key_length, char* buffer,
        size_t* in_out_size);
    uint32_t (*write_json)(void* service_context, uint32_t scope,
        const char* key, size_t key_length, const char* json,
        size_t json_length);
    uint32_t (*remove)(void* service_context, uint32_t scope,
        const char* key, size_t key_length);
} DraxulPluginStorageServiceV2;

typedef struct DraxulPluginViewportV2
{
    uint32_t struct_size;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    float pixel_scale;
    float display_ppi;
} DraxulPluginViewportV2;

typedef struct DraxulPluginInputEventV2
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
} DraxulPluginInputEventV2;

// Optional host services are copied into the caller-provided table. A service
// query returns non-zero only when the complete requested table was supplied.
typedef int32_t (*DraxulPluginQueryHostServiceFnV2)(void* host_context,
    const char* service_id, size_t service_id_length, uint32_t requested_version,
    void* service_table, size_t service_table_size);

typedef struct DraxulPluginHostApiV2
{
    uint32_t struct_size;
    uint32_t abi_version;
    void* host_context;
    // Thread-safe. Requests a render of the pane at the next application frame.
    void (*request_redraw)(void* host_context);
    // Thread-safe. Requests a main-thread tick without implying a render.
    void (*request_tick)(void* host_context);
    // Thread-safe. Invalidates chrome metadata such as status and background.
    void (*notify_presentation_changed)(void* host_context);
    void (*log)(void* host_context, uint32_t level,
        const char* message, size_t message_length);
    DraxulPluginQueryHostServiceFnV2 query_service;
} DraxulPluginHostApiV2;

typedef struct DraxulPluginCreateInfoV2
{
    uint32_t struct_size;
    const DraxulPluginHostApiV2* host;
    const char* plugin_id;
    const char* plugin_directory_utf8;
    const char* config_json;
    size_t config_json_length;
    DraxulPluginViewportV2 initial_viewport;
} DraxulPluginCreateInfoV2;

typedef struct DraxulPluginTickInfoV2
{
    uint32_t struct_size;
    double monotonic_seconds;
    int32_t visible;
    int32_t focused;
} DraxulPluginTickInfoV2;

typedef struct DraxulPluginTickResultV2
{
    uint32_t struct_size;
    uint64_t next_tick_delay_ns;
    int32_t request_redraw;
    int32_t ok;
    const char* error_message;
} DraxulPluginTickResultV2;

typedef struct DraxulPluginVulkanFrameV2
{
    uint32_t struct_size;
    // No render pass is active. Every handle is borrowed for this callback.
    // The target is in VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL on entry.
    // Plugins may create passes on command_buffer but must end them and return
    // the target to that layout. They must not submit, present, retain, or
    // destroy any borrowed object.
    void* instance;
    void* physical_device;
    void* device;
    // Borrowed graphics queue identity is supplied for resource ownership and
    // allocator setup. Plugins must never submit to or wait on this queue.
    void* graphics_queue;
    uint32_t graphics_queue_family;
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
    DraxulPluginViewportV2 viewport;
    double monotonic_seconds;
} DraxulPluginVulkanFrameV2;

typedef struct DraxulPluginMetalFrameV2
{
    uint32_t struct_size;
    // No encoder is active. Every object is unretained and borrowed for this
    // callback. continuation_render_pass_descriptor uses Load/Store and fills
    // the drawable. Plugins must end every encoder they create and must not
    // commit, present, retain, release, or destroy host-owned objects.
    void* device;
    void* command_buffer;
    void* drawable_texture;
    void* continuation_render_pass_descriptor;
    uint64_t target_generation;
    uint32_t frame_index;
    uint32_t buffered_frame_count;
    int32_t framebuffer_width;
    int32_t framebuffer_height;
    DraxulPluginViewportV2 viewport;
    double monotonic_seconds;
} DraxulPluginMetalFrameV2;

typedef struct DraxulPluginRenderResultV2
{
    uint32_t struct_size;
    uint64_t next_frame_delay_ns;
    int32_t ok;
    const char* error_message;
} DraxulPluginRenderResultV2;

typedef struct DraxulPluginPrintHintV2
{
    int32_t content_x;
    int32_t content_y;
    int32_t content_width;
    int32_t content_height;
    int32_t paper_white;
} DraxulPluginPrintHintV2;

// String views returned in this snapshot need remain valid only until
// get_state returns; Draxul copies them immediately.
typedef struct DraxulPluginPresentationStateV2
{
    uint32_t struct_size;
    DraxulPluginStringViewV2 display_name;
    DraxulPluginStringViewV2 status_text;
    float background_red;
    float background_green;
    float background_blue;
    float background_alpha;
    int32_t content_ready;
    uint32_t mouse_cursor;
    DraxulPluginPrintHintV2 print_hint;
} DraxulPluginPresentationStateV2;

typedef struct DraxulPluginPresentationExtensionV2
{
    uint32_t struct_size;
    uint32_t extension_version;
    int32_t (*get_state)(void* instance,
        DraxulPluginPresentationStateV2* state);
    int32_t (*dispatch_action)(void* instance,
        const char* action, size_t action_length);
    size_t (*action_count)(void* instance);
    int32_t (*action_at)(void* instance, size_t index,
        DraxulPluginStringViewV2* action_id,
        DraxulPluginStringViewV2* display_name);
} DraxulPluginPresentationExtensionV2;

typedef int32_t (*DraxulPluginQueryExtensionFnV2)(void* instance,
    const char* extension_id, size_t extension_id_length,
    uint32_t requested_version, void* extension_table,
    size_t extension_table_size);

typedef struct DraxulPluginApiV2
{
    uint32_t struct_size;
    uint32_t abi_version;
    const char* plugin_id;
    const char* display_name;
    const char* plugin_version;
    uint32_t supported_backends;
    void* (*create_instance)(const DraxulPluginCreateInfoV2* create_info);
    // Stops background work and external callbacks. GPU resources remain alive
    // until destroy_instance, which Draxul calls only after renderer idle.
    void (*quiesce_instance)(void* instance);
    void (*destroy_instance)(void* instance);
    void (*set_viewport)(void* instance, const DraxulPluginViewportV2* viewport);
    void (*set_visible)(void* instance, int32_t visible);
    void (*set_focused)(void* instance, int32_t focused);
    int32_t (*handle_input)(void* instance, const DraxulPluginInputEventV2* event);
    DraxulPluginTickResultV2 (*tick)(void* instance,
        const DraxulPluginTickInfoV2* tick_info);
    DraxulPluginRenderResultV2 (*render_vulkan)(void* instance,
        const DraxulPluginVulkanFrameV2* frame);
    DraxulPluginRenderResultV2 (*render_metal)(void* instance,
        const DraxulPluginMetalFrameV2* frame);
    DraxulPluginQueryExtensionFnV2 query_extension;
} DraxulPluginApiV2;

typedef const DraxulPluginApiV2* (*DraxulPluginQueryFnV2)(
    uint32_t requested_abi);

DRAXUL_PLUGIN_EXPORT const DraxulPluginApiV2* draxul_plugin_query_v2(
    uint32_t requested_abi);

#ifdef __cplusplus
}
#endif
