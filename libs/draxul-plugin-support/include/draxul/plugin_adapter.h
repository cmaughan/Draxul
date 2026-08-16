#pragma once

#include <draxul/plugin_api.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

// Header-only core of the C-ABI plugin adapter shell. This header depends
// only on the public plugin SDK, nlohmann::json, and the standard library so
// it can ship with the draxul-plugin-sdk install component and serve
// standalone product extractions; helpers that build on HostServices live in
// plugin_adapter_state.h and stay bundled-build-only.

namespace draxul::plugin_support
{

// ~60 Hz pacing shared by animated plugin adapters.
inline constexpr uint64_t kFrameDelayNs = 16'666'667;

// The GPU backend a dual-backend adapter TU serves in this build.
#if defined(__APPLE__)
inline constexpr uint32_t kNativeBackendMask = DRAXUL_PLUGIN_BACKEND_METAL;
#else
inline constexpr uint32_t kNativeBackendMask = DRAXUL_PLUGIN_BACKEND_VULKAN;
#endif

[[nodiscard]] constexpr DraxulPluginRenderResultV2 render_result(
    bool ok, uint64_t deadline, const char* error = nullptr)
{
    return { sizeof(DraxulPluginRenderResultV2), deadline,
        ok ? 1 : 0, error };
}

[[nodiscard]] constexpr DraxulPluginRenderResultV2 render_result(
    bool ok, const char* error = nullptr)
{
    return render_result(ok, DRAXUL_PLUGIN_NO_DEADLINE, error);
}

[[nodiscard]] constexpr DraxulPluginTickResultV2 tick_result(bool ok,
    uint64_t delay, bool redraw = false, const char* error = nullptr)
{
    return { sizeof(DraxulPluginTickResultV2), delay,
        redraw ? 1 : 0, ok ? 1 : 0, error };
}

// kApi table entries for the backend this module build does not serve.
inline DraxulPluginRenderResultV2 unsupported_render_vulkan(void*,
    const DraxulPluginVulkanFrameV2*)
{
    return render_result(false,
        "Vulkan is not supported by this module build");
}

inline DraxulPluginRenderResultV2 unsupported_render_metal(void*,
    const DraxulPluginMetalFrameV2*)
{
    return render_result(false,
        "Metal is not supported by this module build");
}

// Parses the launch configuration. A missing config parses as an empty
// object; invalid JSON returns nullopt. Both parse iterators always come
// from the same buffer (audit bug #9 was a begin/end pair built from two
// separate "{}" literals).
[[nodiscard]] inline std::optional<nlohmann::json> parse_config_json(
    const DraxulPluginCreateInfoV2& info)
{
    constexpr std::string_view kEmptyObject = "{}";
    const char* begin
        = info.config_json ? info.config_json : kEmptyObject.data();
    const char* end = info.config_json
        ? info.config_json + info.config_json_length
        : kEmptyObject.data() + kEmptyObject.size();
    try
    {
        return nlohmann::json::parse(begin, end);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

// Forwards a changed recommended UI font from a UiStyleClient-compatible
// source to any runtime exposing set_imgui_font(path, size_pixels).
template <typename StyleClient, typename Runtime>
void synchronize_ui_style(StyleClient& style, Runtime* runtime)
{
    if (!runtime)
        return;
    if (const auto font = style.poll())
        runtime->set_imgui_font(font->path, font->size_pixels);
}

struct AdapterAction
{
    std::string_view id;
    std::string_view display_name;
};

// Owns the presentation-extension plumbing every adapter used to hand-roll:
// action_count/action_at over a static action table plus the query_extension
// gatekeeping and extension-table assembly. The plugin supplies its static
// table and its get_state/dispatch_action callbacks:
//
//   constexpr AdapterAction kActions[] = { { "id", "Name" }, ... };
//   using Presentation = PresentationAdapter<kActions,
//       &get_presentation_state, &dispatch_action>;
//   ... .query_extension = &Presentation::query_extension ...
template <const auto& Actions, auto GetState, auto DispatchAction>
struct PresentationAdapter
{
    static size_t action_count(void*)
    {
        return std::size(Actions);
    }

    static int32_t action_at(void*, size_t index,
        DraxulPluginStringViewV2* action_id,
        DraxulPluginStringViewV2* display_name)
    {
        if (!action_id || !display_name || index >= std::size(Actions))
            return 0;
        *action_id = { Actions[index].id.data(), Actions[index].id.size() };
        *display_name = { Actions[index].display_name.data(),
            Actions[index].display_name.size() };
        return 1;
    }

    static int32_t query_extension(void*, const char* extension_id,
        size_t extension_id_length, uint32_t requested_version,
        void* extension_table, size_t extension_table_size)
    {
        if (!extension_id || !extension_table
            || std::string_view(extension_id, extension_id_length)
                != DRAXUL_PLUGIN_PRESENTATION_EXTENSION_ID
            || requested_version
                != DRAXUL_PLUGIN_PRESENTATION_EXTENSION_VERSION
            || extension_table_size
                < sizeof(DraxulPluginPresentationExtensionV2))
            return 0;
        *static_cast<DraxulPluginPresentationExtensionV2*>(extension_table)
            = { sizeof(DraxulPluginPresentationExtensionV2),
                  DRAXUL_PLUGIN_PRESENTATION_EXTENSION_VERSION,
                  GetState, DispatchAction, &action_count, &action_at };
        return 1;
    }
};

struct AdapterIdentity
{
    const char* plugin_id = nullptr;
    const char* display_name = nullptr;
    const char* plugin_version = nullptr;
    uint32_t supported_backends = kNativeBackendMask;
};

// Entry points for make_plugin_api. The render entries default to the
// unsupported stubs so a single-backend adapter only names the one it built.
struct AdapterEntryPoints
{
    void* (*create_instance)(const DraxulPluginCreateInfoV2*) = nullptr;
    void (*quiesce_instance)(void*) = nullptr;
    void (*destroy_instance)(void*) = nullptr;
    void (*set_viewport)(void*, const DraxulPluginViewportV2*) = nullptr;
    void (*set_visible)(void*, int32_t) = nullptr;
    void (*set_focused)(void*, int32_t) = nullptr;
    int32_t (*handle_input)(void*, const DraxulPluginInputEventV2*) = nullptr;
    DraxulPluginTickResultV2 (*tick)(void*, const DraxulPluginTickInfoV2*)
        = nullptr;
    DraxulPluginRenderResultV2 (*render_vulkan)(void*,
        const DraxulPluginVulkanFrameV2*)
        = &unsupported_render_vulkan;
    DraxulPluginRenderResultV2 (*render_metal)(void*,
        const DraxulPluginMetalFrameV2*)
        = &unsupported_render_metal;
    DraxulPluginQueryExtensionFnV2 query_extension = nullptr;
};

[[nodiscard]] constexpr DraxulPluginApiV2 make_plugin_api(
    const AdapterIdentity& identity, const AdapterEntryPoints& entries)
{
    DraxulPluginApiV2 api{};
    api.struct_size = sizeof(DraxulPluginApiV2);
    api.abi_version = DRAXUL_PLUGIN_ABI_VERSION;
    api.plugin_id = identity.plugin_id;
    api.display_name = identity.display_name;
    api.plugin_version = identity.plugin_version;
    api.supported_backends = identity.supported_backends;
    api.create_instance = entries.create_instance;
    api.quiesce_instance = entries.quiesce_instance;
    api.destroy_instance = entries.destroy_instance;
    api.set_viewport = entries.set_viewport;
    api.set_visible = entries.set_visible;
    api.set_focused = entries.set_focused;
    api.handle_input = entries.handle_input;
    api.tick = entries.tick;
    api.render_vulkan = entries.render_vulkan;
    api.render_metal = entries.render_metal;
    api.query_extension = entries.query_extension;
    return api;
}

} // namespace draxul::plugin_support
