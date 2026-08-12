#include <draxul/plugin_api.h>

#include <string>
#include <string_view>
#include <vector>

#ifndef DRAXUL_FIXTURE_PLUGIN_ID
#define DRAXUL_FIXTURE_PLUGIN_ID "dev.draxul.fixture"
#endif
#ifndef DRAXUL_FIXTURE_BACKENDS
#define DRAXUL_FIXTURE_BACKENDS \
    (DRAXUL_PLUGIN_BACKEND_VULKAN | DRAXUL_PLUGIN_BACKEND_METAL)
#endif

namespace
{

struct FixtureInstance
{
    const DraxulPluginHostApiV2* host = nullptr;
    bool visible = true;
    bool focused = false;
    std::string status = "ready";
};

struct CapturedEvent
{
    DraxulPluginInputEventV2 event{};
    std::string text;
};

std::vector<CapturedEvent> captured_events;
FixtureInstance* live_instance = nullptr;
size_t tick_count = 0;
size_t quiesce_count = 0;
size_t action_dispatch_count = 0;

void* create_instance(const DraxulPluginCreateInfoV2* info)
{
    if (!info || !info->host)
        return nullptr;
    auto* instance = new FixtureInstance;
    instance->host = info->host;
    live_instance = instance;
    return instance;
}

void quiesce_instance(void*)
{
    ++quiesce_count;
}

void destroy_instance(void* opaque)
{
    if (live_instance == opaque)
        live_instance = nullptr;
    delete static_cast<FixtureInstance*>(opaque);
}

void set_visible(void* opaque, int32_t visible)
{
    auto* instance = static_cast<FixtureInstance*>(opaque);
    if (instance)
        instance->visible = visible != 0;
}

void set_focused(void* opaque, int32_t focused)
{
    auto* instance = static_cast<FixtureInstance*>(opaque);
    if (instance)
        instance->focused = focused != 0;
}

int32_t handle_input(void*, const DraxulPluginInputEventV2* event)
{
    if (!event)
        return 0;
    CapturedEvent captured;
    captured.event = *event;
    if (event->text_utf8 && event->text_length)
        captured.text.assign(event->text_utf8, event->text_length);
    captured_events.push_back(std::move(captured));
    return 1;
}

DraxulPluginTickResultV2 tick(void*, const DraxulPluginTickInfoV2*)
{
    ++tick_count;
    return { sizeof(DraxulPluginTickResultV2),
        DRAXUL_PLUGIN_NO_DEADLINE, 1, 1, nullptr };
}

DraxulPluginRenderResultV2 render_vulkan(void*,
    const DraxulPluginVulkanFrameV2*)
{
    return { sizeof(DraxulPluginRenderResultV2),
        DRAXUL_PLUGIN_NO_DEADLINE, 1, nullptr };
}

DraxulPluginRenderResultV2 render_metal(void*,
    const DraxulPluginMetalFrameV2*)
{
    return { sizeof(DraxulPluginRenderResultV2),
        DRAXUL_PLUGIN_NO_DEADLINE, 1, nullptr };
}

int32_t get_presentation_state(void* opaque,
    DraxulPluginPresentationStateV2* state)
{
    auto* instance = static_cast<FixtureInstance*>(opaque);
    if (!instance || !state
        || state->struct_size < sizeof(DraxulPluginPresentationStateV2))
        return 0;
    instance->status = instance->visible ? "ready" : "hidden";
    if (instance->focused)
        instance->status += " | focused";
    *state = {};
    state->struct_size = sizeof(*state);
    state->display_name = { "Fixture instance", 16 };
    state->status_text = {
        instance->status.data(), instance->status.size() };
    state->background_red = 0.1f;
    state->background_green = 0.2f;
    state->background_blue = 0.3f;
    state->background_alpha = 1.0f;
    state->content_ready = 1;
    state->mouse_cursor = DRAXUL_PLUGIN_CURSOR_POINTER;
    state->print_hint = { 1, 2, 30, 40, 1 };
    return 1;
}

int32_t dispatch_action(void* opaque, const char* action,
    size_t action_length)
{
    auto* instance = static_cast<FixtureInstance*>(opaque);
    if (!instance || !action
        || std::string_view(action, action_length) != "fixture_action")
        return 0;
    ++action_dispatch_count;
    instance->status = "action dispatched";
    if (instance->host->notify_presentation_changed)
    {
        instance->host->notify_presentation_changed(
            instance->host->host_context);
    }
    return 1;
}

size_t action_count(void*)
{
    return 1;
}

int32_t action_at(void*, size_t index,
    DraxulPluginStringViewV2* action_id,
    DraxulPluginStringViewV2* display_name)
{
    if (index != 0 || !action_id || !display_name)
        return 0;
    *action_id = { "fixture_action", 14 };
    *display_name = { "Fixture Action", 14 };
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
    .plugin_id = DRAXUL_FIXTURE_PLUGIN_ID,
    .display_name = "Fixture",
    .plugin_version = "1.0.0",
    .supported_backends = DRAXUL_FIXTURE_BACKENDS,
    .create_instance = &create_instance,
    .quiesce_instance = &quiesce_instance,
    .destroy_instance = &destroy_instance,
    .set_viewport = nullptr,
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
#ifdef DRAXUL_FIXTURE_REJECT_ABI
    (void)requested_abi;
    return nullptr;
#else
    return requested_abi == DRAXUL_PLUGIN_ABI_VERSION ? &kApi : nullptr;
#endif
}

extern "C" DRAXUL_PLUGIN_EXPORT void draxul_fixture_reset_events()
{
    captured_events.clear();
    tick_count = 0;
    quiesce_count = 0;
    action_dispatch_count = 0;
}

extern "C" DRAXUL_PLUGIN_EXPORT size_t draxul_fixture_event_count()
{
    return captured_events.size();
}

extern "C" DRAXUL_PLUGIN_EXPORT int draxul_fixture_event_at(
    size_t index, DraxulPluginInputEventV2* event)
{
    if (!event || index >= captured_events.size())
        return 0;
    *event = captured_events[index].event;
    event->text_utf8 = captured_events[index].text.c_str();
    event->text_length = captured_events[index].text.size();
    return 1;
}

extern "C" DRAXUL_PLUGIN_EXPORT void draxul_fixture_request_tick()
{
    if (live_instance && live_instance->host->request_tick)
        live_instance->host->request_tick(live_instance->host->host_context);
}

extern "C" DRAXUL_PLUGIN_EXPORT size_t draxul_fixture_tick_count()
{
    return tick_count;
}

extern "C" DRAXUL_PLUGIN_EXPORT size_t draxul_fixture_quiesce_count()
{
    return quiesce_count;
}

extern "C" DRAXUL_PLUGIN_EXPORT size_t draxul_fixture_action_dispatch_count()
{
    return action_dispatch_count;
}
