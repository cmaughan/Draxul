#include <draxul/imgui_host.h>
#include <draxul/plugin_api.h>
#include <draxul/scoreview/score_runtime.h>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace
{

constexpr const char* kPluginId = "dev.draxul.scoreview";
constexpr uint64_t kActiveDelayNs = 16'666'667;
constexpr uint64_t kWorkerDelayNs = 100'000'000;

class RuntimeCallbacks final
    : public draxul::scoreview::ScoreRuntimeCallbacks
{
public:
    void request_frame() override
    {
        if (visible && host && host->request_redraw)
            host->request_redraw(host->host_context);
    }
    void notify_presentation_changed() override
    {
        if (host && host->notify_presentation_changed)
            host->notify_presentation_changed(host->host_context);
    }
    const DraxulPluginHostApiV2* host = nullptr;
    bool visible = true;
};

class OverlayAdapter final : public draxul::IImGuiHost
{
public:
    bool discover(const DraxulPluginHostApiV2& host)
    {
        if (!host.query_service)
            return false;
        service_ = {};
        service_.struct_size = sizeof(service_);
        service_.service_version
            = DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_VERSION;
        available_ = host.query_service(host.host_context,
            DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_ID,
            std::strlen(DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_ID),
            DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_VERSION,
            &service_, sizeof(service_)) != 0
            && service_.imgui_version_num == IMGUI_VERSION_NUM
            && service_.draw_vert_size == sizeof(ImDrawVert)
            && service_.draw_idx_size == sizeof(ImDrawIdx);
        return available_;
    }
    bool initialize_imgui_backend() override
    {
        context_ = ImGui::GetCurrentContext();
        initialized_ = available_ && context_ && service_.initialize(
            service_.service_context, context_) != 0;
        return initialized_;
    }
    void shutdown_imgui_backend() override
    {
        if (initialized_)
            service_.shutdown(service_.service_context, context_);
        initialized_ = false;
        context_ = nullptr;
    }
    void rebuild_imgui_font_texture() override
    {
        if (initialized_)
            service_.rebuild_font_texture(service_.service_context, context_);
    }
    void begin_imgui_frame() override
    {
        if (initialized_ || initialize_imgui_backend())
            service_.begin_frame(service_.service_context, context_);
    }
    bool render_imgui_draw_data(const ImDrawData* data,
        ImGuiContext* context) override
    {
        return initialized_ && service_.render_draw_data(
            service_.service_context, const_cast<ImDrawData*>(data),
            context) != 0;
    }
    void synchronize_font(draxul::scoreview::ScoreRuntime& runtime)
    {
        if (!available_)
            return;
        size_t required = 0;
        float size = 0.0f;
        if (!service_.get_font(service_.service_context, nullptr,
                &required, &size) || required == 0)
            return;
        std::string path(required, '\0');
        if (!service_.get_font(service_.service_context, path.data(),
                &required, &size))
            return;
        path.resize(required - 1);
        if (path == font_path_ && size == font_size_)
            return;
        font_path_ = std::move(path);
        font_size_ = size;
        runtime.set_imgui_font(font_path_, font_size_);
    }
    bool available() const { return available_; }

private:
    DraxulPluginImGuiOverlayServiceV2 service_{};
    ImGuiContext* context_ = nullptr;
    std::string font_path_;
    float font_size_ = 0.0f;
    bool available_ = false;
    bool initialized_ = false;
};

struct Instance;

class FrameSink final : public draxul::scoreview::ScoreFrameSink
{
public:
    explicit FrameSink(Instance& instance) : instance_(instance) {}
    void record_canvas(draxul::INanoVGPass&, int, int, int, int) override {}
    void render_overlay(void* draw_data, void* context) override;
    void finish() override {}
private:
    Instance& instance_;
};

struct Instance
{
    const DraxulPluginHostApiV2* host = nullptr;
    DraxulPluginCanvas2DServiceV2 canvas{};
    DraxulPluginPathServiceV2 paths{};
    DraxulPluginViewportV2 viewport{};
    RuntimeCallbacks callbacks;
    OverlayAdapter overlay;
    std::unique_ptr<draxul::scoreview::ScoreRuntime> runtime;
    std::filesystem::path directory;
    std::string status;
    bool visible = true;
    bool focused = false;
    bool background_playback = false;
    bool quiesced = false;
};

void FrameSink::render_overlay(void* draw_data, void* context)
{
    if (instance_.canvas.set_overlay)
        instance_.canvas.set_overlay(instance_.canvas.service_context,
            draw_data, context);
}

std::string service_path(DraxulPluginPathServiceV2& service,
    uint32_t kind)
{
    size_t required = 0;
    if (!service.get_path || !service.get_path(service.service_context,
            kind, nullptr, &required) || required == 0)
        return {};
    std::string value(required, '\0');
    if (!service.get_path(service.service_context, kind,
            value.data(), &required))
        return {};
    value.resize(required - 1);
    return value;
}

DraxulPluginTickResultV2 tick_result(bool ok, uint64_t delay,
    bool redraw = false, const char* error = nullptr)
{
    return { sizeof(DraxulPluginTickResultV2), delay,
        redraw ? 1 : 0, ok ? 1 : 0, error };
}

DraxulPluginRenderResultV2 render_result(bool ok,
    const char* error = nullptr)
{
    return { sizeof(DraxulPluginRenderResultV2),
        DRAXUL_PLUGIN_NO_DEADLINE, ok ? 1 : 0, error };
}

void* create_instance(const DraxulPluginCreateInfoV2* info)
{
    if (!info || !info->host || !info->host->query_service)
        return nullptr;
    auto instance = std::make_unique<Instance>();
    instance->host = info->host;
    instance->callbacks.host = info->host;
    instance->viewport = info->initial_viewport;
    instance->directory = info->plugin_directory_utf8
        ? std::filesystem::u8path(info->plugin_directory_utf8)
        : std::filesystem::path{};

    instance->canvas.struct_size = sizeof(instance->canvas);
    instance->canvas.service_version = DRAXUL_PLUGIN_CANVAS2D_SERVICE_VERSION;
    if (!info->host->query_service(info->host->host_context,
            DRAXUL_PLUGIN_CANVAS2D_SERVICE_ID,
            std::strlen(DRAXUL_PLUGIN_CANVAS2D_SERVICE_ID),
            DRAXUL_PLUGIN_CANVAS2D_SERVICE_VERSION,
            &instance->canvas, sizeof(instance->canvas))
        || instance->canvas.cpp_abi_fingerprint
            != DRAXUL_PLUGIN_CANVAS2D_CPP_ABI_FINGERPRINT)
        return nullptr;

    instance->paths.struct_size = sizeof(instance->paths);
    instance->paths.service_version = DRAXUL_PLUGIN_PATH_SERVICE_VERSION;
    info->host->query_service(info->host->host_context,
        DRAXUL_PLUGIN_PATH_SERVICE_ID,
        std::strlen(DRAXUL_PLUGIN_PATH_SERVICE_ID),
        DRAXUL_PLUGIN_PATH_SERVICE_VERSION,
        &instance->paths, sizeof(instance->paths));
    instance->overlay.discover(*info->host);

    std::string source;
    std::string mode = "paged";
    try
    {
        const auto config = nlohmann::json::parse(
            info->config_json ? info->config_json : "{}",
            info->config_json
                ? info->config_json + info->config_json_length : "{}" + 2);
        source = config.value("source", std::string{});
        mode = config.value("mode", mode);
        instance->background_playback
            = config.value("background_playback", false);
    }
    catch (...)
    {
        return nullptr;
    }

    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Score;
    context.launch_options.source_path = source;
    context.launch_options.command = mode;
    context.initial_viewport.pixel_pos = {
        info->initial_viewport.x, info->initial_viewport.y };
    context.initial_viewport.pixel_size = {
        info->initial_viewport.width, info->initial_viewport.height };
    context.initial_viewport.pixel_scale = info->initial_viewport.pixel_scale;
    draxul::scoreview::ScoreRuntimePaths paths;
    paths.verovio_data = instance->directory / "verovio-data";
    paths.soundfonts = instance->directory / "soundfonts";
    const std::string data = service_path(instance->paths,
        DRAXUL_PLUGIN_PATH_DATA);
    if (!data.empty())
        paths.progress = std::filesystem::u8path(data) / "progress";
    instance->runtime = std::make_unique<draxul::scoreview::ScoreRuntime>();
    if (!instance->runtime->initialize(context, instance->callbacks,
            std::move(paths)))
        return nullptr;
    if (instance->overlay.available())
        instance->runtime->attach_imgui_host(instance->overlay);
    if (!instance->canvas.set_render_pass(
            instance->canvas.service_context,
            instance->runtime->canvas_pass(),
            DRAXUL_PLUGIN_CANVAS2D_CPP_ABI_FINGERPRINT))
        return nullptr;
    return instance.release();
}

void quiesce_instance(void* opaque)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || instance->quiesced)
        return;
    instance->runtime->quiesce();
    instance->quiesced = true;
}

void destroy_instance(void* opaque)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance)
        return;
    instance->runtime->shutdown();
    delete instance;
}

void set_viewport(void* opaque, const DraxulPluginViewportV2* viewport)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !viewport)
        return;
    instance->viewport = *viewport;
    draxul::HostViewport value;
    value.pixel_pos = { viewport->x, viewport->y };
    value.pixel_size = { viewport->width, viewport->height };
    value.pixel_scale = viewport->pixel_scale;
    instance->runtime->set_viewport(value);
}

void set_visible(void* opaque, int32_t visible)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance)
        return;
    instance->visible = visible != 0;
    instance->callbacks.visible = instance->visible;
    instance->runtime->set_presentation_visible(instance->visible,
        instance->background_playback);
    if (instance->host->request_tick)
        instance->host->request_tick(instance->host->host_context);
    if (instance->visible && instance->host->request_redraw)
        instance->host->request_redraw(instance->host->host_context);
}

void set_focused(void* opaque, int32_t focused)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (instance)
        instance->focused = focused != 0;
}

int32_t handle_input(void* opaque, const DraxulPluginInputEventV2* event)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !event)
        return 0;
    const glm::ivec2 position{
        event->x + instance->viewport.x,
        event->y + instance->viewport.y };
    switch (event->kind)
    {
    case DRAXUL_PLUGIN_INPUT_KEY:
        instance->runtime->on_key({ static_cast<int>(event->physical_key),
            event->logical_key,
            static_cast<draxul::ModifierFlags>(event->modifiers),
            event->pressed != 0 });
        return 1;
    case DRAXUL_PLUGIN_INPUT_TEXT:
        instance->runtime->on_text_input({ std::string(
            event->text_utf8 ? event->text_utf8 : "",
            event->text_utf8 ? event->text_length : 0) });
        return 1;
    case DRAXUL_PLUGIN_INPUT_WHEEL:
        instance->runtime->on_mouse_wheel({
            { event->delta_x, event->delta_y },
            static_cast<draxul::ModifierFlags>(event->modifiers), position });
        return 1;
    case DRAXUL_PLUGIN_INPUT_POINTER_BUTTON:
        instance->runtime->on_mouse_button({ event->button,
            event->pressed != 0,
            static_cast<draxul::ModifierFlags>(event->modifiers), position,
            event->clicks });
        return 1;
    case DRAXUL_PLUGIN_INPUT_POINTER_MOVE:
        instance->runtime->on_mouse_move({
            static_cast<draxul::ModifierFlags>(event->modifiers), position,
            { event->delta_x, event->delta_y }, event->buttons });
        return 1;
    default:
        return 0;
    }
}

DraxulPluginTickResultV2 tick(void* opaque,
    const DraxulPluginTickInfoV2* info)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !info || instance->quiesced)
        return tick_result(instance != nullptr, DRAXUL_PLUGIN_NO_DEADLINE);
    instance->overlay.synchronize_font(*instance->runtime);
    instance->runtime->pump();
    const auto deadline = instance->runtime->next_deadline();
    if (!instance->visible || !info->visible)
        return tick_result(true, deadline
                ? (instance->background_playback
                    ? kActiveDelayNs : kWorkerDelayNs)
                                          : DRAXUL_PLUGIN_NO_DEADLINE);
    return tick_result(true, deadline ? kActiveDelayNs
                                      : DRAXUL_PLUGIN_NO_DEADLINE,
        deadline.has_value());
}

DraxulPluginRenderResultV2 render(void* opaque)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !instance->visible || instance->quiesced)
        return render_result(true);
    FrameSink sink(*instance);
    instance->runtime->draw(sink);
    return render_result(true);
}

DraxulPluginRenderResultV2 render_vulkan(void* opaque,
    const DraxulPluginVulkanFrameV2*) { return render(opaque); }
DraxulPluginRenderResultV2 render_metal(void* opaque,
    const DraxulPluginMetalFrameV2*) { return render(opaque); }

int32_t get_state(void* opaque, DraxulPluginPresentationStateV2* state)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !state || state->struct_size < sizeof(*state))
        return 0;
    instance->status = instance->runtime->status_text();
    *state = {};
    state->struct_size = sizeof(*state);
    state->display_name = { "ScoreView", 9 };
    state->status_text = { instance->status.data(), instance->status.size() };
    const auto background = instance->runtime->default_background();
    state->background_red = background.r;
    state->background_green = background.g;
    state->background_blue = background.b;
    state->background_alpha = background.a;
    state->content_ready = instance->runtime->runtime_state().content_ready;
    const auto hint = instance->runtime->print_hint();
    state->print_hint = { hint.content_pos.x, hint.content_pos.y,
        hint.content_size.x, hint.content_size.y, hint.paper_white ? 1 : 0 };
    return 1;
}

int32_t dispatch_action(void* opaque, const char* action, size_t length)
{
    auto* instance = static_cast<Instance*>(opaque);
    return instance && action && instance->runtime->dispatch_action(
        std::string_view(action, length)) ? 1 : 0;
}

constexpr std::string_view kActions[] = {
    "font_increase", "font_decrease", "font_reset" };
constexpr std::string_view kActionNames[] = {
    "Zoom In", "Zoom Out", "Reset Zoom" };
size_t action_count(void*) { return std::size(kActions); }
int32_t action_at(void*, size_t index, DraxulPluginStringViewV2* id,
    DraxulPluginStringViewV2* name)
{
    if (!id || !name || index >= std::size(kActions))
        return 0;
    *id = { kActions[index].data(), kActions[index].size() };
    *name = { kActionNames[index].data(), kActionNames[index].size() };
    return 1;
}
int32_t query_extension(void*, const char* id, size_t length,
    uint32_t version, void* table, size_t table_size)
{
    if (!id || !table || std::string_view(id, length)
            != DRAXUL_PLUGIN_PRESENTATION_EXTENSION_ID
        || version != DRAXUL_PLUGIN_PRESENTATION_EXTENSION_VERSION
        || table_size < sizeof(DraxulPluginPresentationExtensionV2))
        return 0;
    auto* extension = static_cast<DraxulPluginPresentationExtensionV2*>(table);
    *extension = { sizeof(*extension),
        DRAXUL_PLUGIN_PRESENTATION_EXTENSION_VERSION, &get_state,
        &dispatch_action, &action_count, &action_at };
    return 1;
}

const DraxulPluginApiV2 kApi{
    sizeof(DraxulPluginApiV2), DRAXUL_PLUGIN_ABI_VERSION,
    kPluginId, "ScoreView", "0.1.0",
    DRAXUL_PLUGIN_BACKEND_VULKAN | DRAXUL_PLUGIN_BACKEND_METAL,
    &create_instance, &quiesce_instance, &destroy_instance,
    &set_viewport, &set_visible, &set_focused, &handle_input, &tick,
    &render_vulkan, &render_metal, &query_extension };

} // namespace

extern "C" DRAXUL_PLUGIN_EXPORT const DraxulPluginApiV2*
draxul_plugin_query_v2(uint32_t requested_abi)
{
    return requested_abi == DRAXUL_PLUGIN_ABI_VERSION ? &kApi : nullptr;
}
