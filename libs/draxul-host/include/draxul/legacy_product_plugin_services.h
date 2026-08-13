#pragma once

// Temporary source-tree-only compatibility surface for product plugins that
// have not yet moved to the public raw GPU callbacks. This header is never
// installed with DraxulPluginSDK and must be deleted with the old product
// module layer. New or external plugins must not use it.

#include <stddef.h>
#include <stdint.h>

#define DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_ID "draxul.imgui-overlay"
#define DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_VERSION 1u
#define DRAXUL_PLUGIN_CANVAS2D_SERVICE_ID "draxul.canvas2d"
#define DRAXUL_PLUGIN_CANVAS2D_SERVICE_VERSION 1u
#define DRAXUL_PLUGIN_CANVAS2D_CPP_ABI_FINGERPRINT UINT64_C(0x4452584c43324401)

typedef struct DraxulPluginImGuiOverlayServiceV2
{
    uint32_t struct_size;
    uint32_t service_version;
    uint32_t imgui_version_num;
    uint32_t draw_vert_size;
    uint32_t draw_idx_size;
    void* service_context;
    int32_t (*initialize)(void* service_context, void* imgui_context);
    void (*shutdown)(void* service_context, void* imgui_context);
    void (*rebuild_font_texture)(void* service_context, void* imgui_context);
    int32_t (*begin_frame)(void* service_context, void* imgui_context);
    int32_t (*render_draw_data)(void* service_context, void* draw_data,
        void* imgui_context);
    int32_t (*get_font)(void* service_context, char* path,
        size_t* in_out_size, float* size_pixels);
} DraxulPluginImGuiOverlayServiceV2;

typedef struct DraxulPluginCanvas2DServiceV2
{
    uint32_t struct_size;
    uint32_t service_version;
    uint64_t cpp_abi_fingerprint;
    void* service_context;
    int32_t (*set_render_pass)(void* service_context,
        void* opaque_render_pass, uint64_t cpp_abi_fingerprint);
    void (*set_overlay)(void* service_context, void* draw_data,
        void* imgui_context);
} DraxulPluginCanvas2DServiceV2;
