#include <draxul/plugin_api.h>

#include <string>
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

void* create_instance(const DraxulPluginCreateInfoV1*)
{
    return reinterpret_cast<void*>(1);
}

void destroy_instance(void*) {}

struct CapturedEvent
{
    DraxulPluginInputEventV1 event{};
    std::string text;
};

std::vector<CapturedEvent> captured_events;

int32_t handle_input(void*, const DraxulPluginInputEventV1* event)
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

DraxulPluginRenderResultV1 render_vulkan(void*,
    const DraxulPluginVulkanFrameV1*)
{
    return { sizeof(DraxulPluginRenderResultV1),
        DRAXUL_PLUGIN_NO_DEADLINE, 1, nullptr };
}

DraxulPluginRenderResultV1 render_metal(void*,
    const DraxulPluginMetalFrameV1*)
{
    return { sizeof(DraxulPluginRenderResultV1),
        DRAXUL_PLUGIN_NO_DEADLINE, 1, nullptr };
}

const DraxulPluginApiV1 kApi = {
    sizeof(DraxulPluginApiV1), DRAXUL_PLUGIN_ABI_V1,
    DRAXUL_FIXTURE_PLUGIN_ID, "Fixture", "1.0.0",
    DRAXUL_FIXTURE_BACKENDS,
    &create_instance, &destroy_instance,
    nullptr, nullptr, nullptr, &handle_input,
    &render_vulkan, &render_metal,
};

} // namespace

extern "C" DRAXUL_PLUGIN_EXPORT const DraxulPluginApiV1*
draxul_plugin_query(uint32_t requested_abi)
{
#ifdef DRAXUL_FIXTURE_REJECT_ABI
    (void)requested_abi;
    return nullptr;
#else
    return requested_abi == DRAXUL_PLUGIN_ABI_V1 ? &kApi : nullptr;
#endif
}

extern "C" DRAXUL_PLUGIN_EXPORT void draxul_fixture_reset_events()
{
    captured_events.clear();
}

extern "C" DRAXUL_PLUGIN_EXPORT size_t draxul_fixture_event_count()
{
    return captured_events.size();
}

extern "C" DRAXUL_PLUGIN_EXPORT int draxul_fixture_event_at(
    size_t index, DraxulPluginInputEventV1* event)
{
    if (!event || index >= captured_events.size())
        return 0;
    *event = captured_events[index].event;
    event->text_utf8 = captured_events[index].text.c_str();
    event->text_length = captured_events[index].text.size();
    return 1;
}
