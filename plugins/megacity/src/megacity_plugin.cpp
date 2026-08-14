#include <draxul/plugin_api.h>
#include <draxul/plugin_gpu_imgui.h>
#include <draxul/base_renderer.h>
#include <draxul/codeviz_scene_pass.h>
#include <draxul/megacity_host.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>

#if defined(__APPLE__)
#include <draxul/metal/metal_render_context.h>
#import <Metal/Metal.h>
#else
#include <draxul/vulkan/vk_render_context.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#endif

namespace
{

constexpr const char* kPluginId = "dev.draxul.megacity";

struct Callbacks final : draxul::IHostCallbacks
{
    void request_frame() override
    {
        if (!api)
            return;
        // IHost's request_frame historically guaranteed that the main loop
        // would both pump product state and redraw it. Preserve both halves
        // across the plugin ABI so completed scanner/route/grid work is
        // consumed before the requested frame is rendered.
        if (api->request_tick)
            api->request_tick(api->host_context);
        if (api->request_redraw)
            api->request_redraw(api->host_context);
    }
    void request_quit() override {}
    void wake_window() override
    {
        if (api && api->request_tick)
            api->request_tick(api->host_context);
    }
    void set_window_title(const std::string&) override {}
    void set_text_input_area(int, int, int, int) override {}
    const DraxulPluginHostApiV2* api = nullptr;
};

class CapturedFrame final : public draxul::IFrameContext
{
public:
    void draw_grid_handle(draxul::IGridHandle&) override {}
    void record_render_pass(draxul::IRenderPass& pass,
        const draxul::RenderViewport& viewport) override
    {
        pass_ = &pass;
        viewport_ = viewport;
    }
    void render_imgui(const ImDrawData* data, ImGuiContext* context) override
    {
        draw_data_ = data;
        imgui_context_ = context;
    }
    void flush_submit_chunk() override {}

    draxul::IRenderPass* pass_ = nullptr;
    draxul::RenderViewport viewport_{};
    const ImDrawData* draw_data_ = nullptr;
    ImGuiContext* imgui_context_ = nullptr;
};

struct Instance
{
    const DraxulPluginHostApiV2* api = nullptr;
    DraxulPluginViewportV2 viewport{};
    draxul::MegaCityVisualizationMode mode = draxul::MegaCityVisualizationMode::City;
    bool visible = true;
    bool focused = false;
    bool quiesced = false;
    std::string status;
    Callbacks callbacks;
    std::unique_ptr<draxul::plugin_support::GpuImGuiHost> imgui;
    draxul::plugin_support::UiStyleClient ui_style;
    std::unique_ptr<draxul::MegaCityHost> host;
#if !defined(__APPLE__)
    VmaAllocator allocator = VK_NULL_HANDLE;
#endif
};

DraxulPluginRenderResultV2 render_result(bool ok,
    const char* error = nullptr)
{
    return { sizeof(DraxulPluginRenderResultV2),
        DRAXUL_PLUGIN_NO_DEADLINE, ok ? 1 : 0, error };
}

DraxulPluginTickResultV2 tick_result(bool ok, uint64_t delay,
    bool redraw = false, const char* error = nullptr)
{
    return { sizeof(DraxulPluginTickResultV2), delay,
        redraw ? 1 : 0, ok ? 1 : 0, error };
}

void notify(Instance* instance)
{
    if (instance && instance->api
        && instance->api->notify_presentation_changed)
        instance->api->notify_presentation_changed(instance->api->host_context);
}

void synchronize_ui_style(Instance& instance)
{
    if (instance.host)
    {
        if (const auto font = instance.ui_style.poll())
            instance.host->set_imgui_font(font->path, font->size_pixels);
    }
}

void* create_instance(const DraxulPluginCreateInfoV2* info)
{
    if (!info || !info->host)
        return nullptr;
    auto instance = std::make_unique<Instance>();
    instance->api = info->host;
    instance->callbacks.api = info->host;
    instance->viewport = info->initial_viewport;

    std::string mode = "city";
    std::string source;
    bool continuous_refresh = false;
    bool show_ui = true;
    try
    {
        const char* begin = info->config_json ? info->config_json : "{}";
        const char* end = info->config_json
            ? info->config_json + info->config_json_length : begin + 2;
        const auto config = nlohmann::json::parse(begin, end);
        mode = config.value("mode", mode);
        source = config.value("source", source);
        continuous_refresh = config.value("continuous_refresh", false);
        show_ui = config.value("show_ui", true);
    }
    catch (...)
    {
        return nullptr;
    }
    if (mode == "biology" || mode == "bioview")
        instance->mode = draxul::MegaCityVisualizationMode::Biology;
    else if (mode != "city" && mode != "megacity")
        return nullptr;

    const std::filesystem::path directory = info->plugin_directory_utf8
        ? std::filesystem::u8path(info->plugin_directory_utf8)
        : std::filesystem::path{};
    draxul::set_codeviz_product_root(directory);
    instance->ui_style.discover(*info->host);
    instance->imgui = draxul::plugin_support::create_gpu_imgui_host();
    instance->host = std::make_unique<draxul::MegaCityHost>(instance->mode);

    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.source_path = source;
    context.launch_options.request_continuous_refresh = continuous_refresh;
    context.launch_options.show_host_ui_panels = show_ui && instance->imgui;
    context.initial_viewport.pixel_pos = {
        info->initial_viewport.x, info->initial_viewport.y };
    context.initial_viewport.pixel_size = {
        info->initial_viewport.width, info->initial_viewport.height };
    context.initial_viewport.pixel_scale = info->initial_viewport.pixel_scale;
    context.display_ppi = info->initial_viewport.display_ppi;
    if (!instance->host->initialize(context, instance->callbacks))
        return nullptr;
    if (instance->imgui)
    {
        instance->host->attach_imgui_host(*instance->imgui);
        synchronize_ui_style(*instance);
    }
    return instance.release();
}

void quiesce_instance(void* opaque)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (instance && !instance->quiesced)
    {
        instance->quiesced = true;
        if (instance->host)
            instance->host->quiesce();
    }
}

void destroy_instance(void* opaque)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance)
        return;
    if (instance->host)
        instance->host->shutdown();
#if !defined(__APPLE__)
    if (instance->allocator)
        vmaDestroyAllocator(instance->allocator);
#endif
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
    instance->host->set_viewport(value);
}

void set_visible(void* opaque, int32_t visible)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance)
        return;
    instance->visible = visible != 0;
    instance->host->set_presentation_visible(instance->visible);
    if (instance->visible && instance->api->request_redraw)
        instance->api->request_redraw(instance->api->host_context);
    notify(instance);
}

void set_focused(void* opaque, int32_t focused)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance)
        return;
    instance->focused = focused != 0;
    if (instance->focused)
        instance->host->on_focus_gained();
    else
        instance->host->on_focus_lost();
    notify(instance);
}

int32_t handle_input(void* opaque, const DraxulPluginInputEventV2* event)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !event || !instance->host)
        return 0;
    const glm::ivec2 pos{ event->x + instance->viewport.x,
        event->y + instance->viewport.y };
    switch (event->kind)
    {
    case DRAXUL_PLUGIN_INPUT_KEY:
        instance->host->on_key({ static_cast<int>(event->physical_key),
            event->logical_key, static_cast<draxul::ModifierFlags>(event->modifiers),
            event->pressed != 0 });
        break;
    case DRAXUL_PLUGIN_INPUT_TEXT:
        instance->host->on_text_input({ std::string(event->text_utf8
            ? event->text_utf8 : "", event->text_utf8 ? event->text_length : 0) });
        break;
    case DRAXUL_PLUGIN_INPUT_COMPOSITION:
        instance->host->on_text_editing({ std::string(event->text_utf8
            ? event->text_utf8 : "", event->text_utf8 ? event->text_length : 0),
            event->composition_start, event->composition_length });
        break;
    case DRAXUL_PLUGIN_INPUT_POINTER_BUTTON:
        instance->host->on_mouse_button({ event->button, event->pressed != 0,
            static_cast<draxul::ModifierFlags>(event->modifiers), pos, event->clicks });
        break;
    case DRAXUL_PLUGIN_INPUT_POINTER_MOVE:
        instance->host->on_mouse_move({
            static_cast<draxul::ModifierFlags>(event->modifiers), pos,
            { event->delta_x, event->delta_y }, event->buttons });
        break;
    case DRAXUL_PLUGIN_INPUT_WHEEL:
        instance->host->on_mouse_wheel({ { event->delta_x, event->delta_y },
            static_cast<draxul::ModifierFlags>(event->modifiers), pos });
        break;
    case DRAXUL_PLUGIN_INPUT_FOCUS:
        set_focused(instance, event->pressed);
        break;
    default:
        return 0;
    }
    return 1;
}

DraxulPluginTickResultV2 tick(void* opaque,
    const DraxulPluginTickInfoV2* info)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !info)
        return tick_result(false, DRAXUL_PLUGIN_NO_DEADLINE,
            false, "MegaCity received an invalid tick");
    if (instance->quiesced || !instance->visible || !info->visible)
        return tick_result(true, DRAXUL_PLUGIN_NO_DEADLINE);
    synchronize_ui_style(*instance);
    instance->host->pump();
    const auto deadline = instance->host->next_deadline();
    if (!deadline)
    {
        // Background scanners do not own UI callbacks. Poll only while the
        // product reports outstanding work, then return to a fully idle pane.
        if (instance->host->requires_periodic_wake())
            return tick_result(true, 50'000'000, false);
        return tick_result(true, DRAXUL_PLUGIN_NO_DEADLINE);
    }
    const auto delay = std::max(std::chrono::steady_clock::duration::zero(),
        *deadline - std::chrono::steady_clock::now());
    return tick_result(true, static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(delay).count()), true);
}

#if !defined(__APPLE__)
bool ensure_allocator(Instance& instance,
    const DraxulPluginVulkanFrameV2& frame)
{
    if (instance.allocator)
        return true;
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo info{};
    info.instance = static_cast<VkInstance>(frame.instance);
    info.physicalDevice = static_cast<VkPhysicalDevice>(frame.physical_device);
    info.device = static_cast<VkDevice>(frame.device);
    info.pVulkanFunctions = &functions;
    info.vulkanApiVersion = VK_API_VERSION_1_2;
    return vmaCreateAllocator(&info, &instance.allocator) == VK_SUCCESS;
}
#endif

DraxulPluginRenderResultV2 render_vulkan(void* opaque,
    const DraxulPluginVulkanFrameV2* frame)
{
#if defined(__APPLE__)
    (void)opaque; (void)frame;
    return render_result(false, "Vulkan is not supported by this module build");
#else
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !frame || !instance->visible || instance->quiesced)
        return render_result(true);
    CapturedFrame captured;
    if (instance->imgui)
        instance->imgui->set_vulkan_frame(frame);
    instance->host->draw(captured);
    if (!captured.pass_)
        return render_result(false, "MegaCity did not provide a scene pass");
    if (!ensure_allocator(*instance, *frame))
        return render_result(false, "MegaCity could not create its Vulkan allocator");
    const auto command_buffer = static_cast<VkCommandBuffer>(frame->command_buffer);
    const auto render_pass = reinterpret_cast<VkRenderPass>(
        static_cast<uintptr_t>(frame->continuation_render_pass));
    draxul::VkRenderContext context(command_buffer,
        static_cast<VkPhysicalDevice>(frame->physical_device),
        static_cast<VkDevice>(frame->device), instance->allocator,
        render_pass, frame->frame_index, frame->buffered_frame_count,
        frame->framebuffer_width, frame->framebuffer_height,
        captured.viewport_.x, captured.viewport_.y,
        std::max(1, captured.viewport_.width), std::max(1, captured.viewport_.height),
        reinterpret_cast<VkImage>(static_cast<uintptr_t>(frame->target_image)),
        reinterpret_cast<VkImageView>(static_cast<uintptr_t>(frame->target_image_view)),
        static_cast<VkFormat>(frame->target_format),
        static_cast<VkQueue>(frame->graphics_queue), frame->graphics_queue_family,
        static_cast<VkInstance>(frame->instance), render_pass,
        reinterpret_cast<VkFramebuffer>(static_cast<uintptr_t>(frame->continuation_framebuffer)),
        static_cast<VkFormat>(frame->depth_format), frame->target_generation);
    captured.pass_->record_prepass(context);
    VkRenderPassBeginInfo begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    begin.renderPass = render_pass;
    begin.framebuffer = reinterpret_cast<VkFramebuffer>(
        static_cast<uintptr_t>(frame->continuation_framebuffer));
    begin.renderArea.extent = { static_cast<uint32_t>(frame->framebuffer_width),
        static_cast<uint32_t>(frame->framebuffer_height) };
    vkCmdBeginRenderPass(command_buffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
    captured.pass_->record(context);
    vkCmdEndRenderPass(command_buffer);
    if (instance->imgui && captured.draw_data_ && captured.imgui_context_)
        instance->imgui->render_imgui_draw_data(
            captured.draw_data_, captured.imgui_context_);
    return render_result(true);
#endif
}

DraxulPluginRenderResultV2 render_metal(void* opaque,
    const DraxulPluginMetalFrameV2* frame)
{
#if !defined(__APPLE__)
    (void)opaque; (void)frame;
    return render_result(false, "Metal is not supported by this module build");
#else
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !frame || !instance->visible || instance->quiesced)
        return render_result(true);
    CapturedFrame captured;
    if (instance->imgui)
        instance->imgui->set_metal_frame(frame);
    instance->host->draw(captured);
    if (!captured.pass_)
        return render_result(false, "MegaCity did not provide a scene pass");
    id<MTLDevice> device = (__bridge id<MTLDevice>)frame->device;
    id<MTLCommandBuffer> command = (__bridge id<MTLCommandBuffer>)frame->command_buffer;
    id<MTLTexture> texture = (__bridge id<MTLTexture>)frame->drawable_texture;
    MTLRenderPassDescriptor* descriptor = (__bridge MTLRenderPassDescriptor*)
        frame->continuation_render_pass_descriptor;
    draxul::MetalRenderContext prepass(command, nil, frame->frame_index,
        frame->buffered_frame_count, frame->framebuffer_width, frame->framebuffer_height,
        captured.viewport_.x, captured.viewport_.y,
        std::max(1, captured.viewport_.width), std::max(1, captured.viewport_.height),
        device, texture, descriptor, frame->target_generation);
    captured.pass_->record_prepass(prepass);
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:descriptor];
    if (!encoder)
        return render_result(false, "MegaCity could not create its Metal encoder");
    draxul::MetalRenderContext context(command, encoder, frame->frame_index,
        frame->buffered_frame_count, frame->framebuffer_width, frame->framebuffer_height,
        captured.viewport_.x, captured.viewport_.y,
        std::max(1, captured.viewport_.width), std::max(1, captured.viewport_.height),
        device, texture, descriptor, frame->target_generation);
    captured.pass_->record(context);
    [encoder endEncoding];
    if (instance->imgui && captured.draw_data_ && captured.imgui_context_)
        instance->imgui->render_imgui_draw_data(
            captured.draw_data_, captured.imgui_context_);
    return render_result(true);
#endif
}

int32_t get_state(void* opaque, DraxulPluginPresentationStateV2* state)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !state || state->struct_size < sizeof(*state))
        return 0;
    instance->status = instance->host ? instance->host->status_text() : "loading";
    if (!instance->visible)
        instance->status += " | hidden";
    const auto bg = instance->host ? instance->host->default_background()
        : draxul::Color{};
    const std::string_view name = instance->mode == draxul::MegaCityVisualizationMode::Biology
        ? std::string_view("BioView") : std::string_view("MegaCity");
    *state = {};
    state->struct_size = sizeof(*state);
    state->display_name = { name.data(), name.size() };
    state->status_text = { instance->status.data(), instance->status.size() };
    state->background_red = bg.r;
    state->background_green = bg.g;
    state->background_blue = bg.b;
    state->background_alpha = bg.a;
    state->content_ready = instance->host
        && instance->host->runtime_state().content_ready && !instance->quiesced;
    state->mouse_cursor = DRAXUL_PLUGIN_CURSOR_DEFAULT;
    return 1;
}

int32_t dispatch_action(void* opaque, const char* action, size_t length)
{
    auto* instance = static_cast<Instance*>(opaque);
    return instance && instance->host && action
        && instance->host->dispatch_action(std::string_view(action, length));
}

constexpr std::string_view kActionIds[] = { "toggle_ui_panels" };
constexpr std::string_view kActionNames[] = { "Toggle Control Panels" };

size_t action_count(void*) { return std::size(kActionIds); }

int32_t action_at(void*, size_t index, DraxulPluginStringViewV2* id,
    DraxulPluginStringViewV2* name)
{
    if (!id || !name || index >= std::size(kActionIds))
        return 0;
    *id = { kActionIds[index].data(), kActionIds[index].size() };
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
    *static_cast<DraxulPluginPresentationExtensionV2*>(table) = {
        sizeof(DraxulPluginPresentationExtensionV2),
        DRAXUL_PLUGIN_PRESENTATION_EXTENSION_VERSION,
        &get_state, &dispatch_action, &action_count, &action_at };
    return 1;
}

#if defined(__APPLE__)
constexpr uint32_t kBackends = DRAXUL_PLUGIN_BACKEND_METAL;
#else
constexpr uint32_t kBackends = DRAXUL_PLUGIN_BACKEND_VULKAN;
#endif

const DraxulPluginApiV2 kApi = {
    sizeof(DraxulPluginApiV2), DRAXUL_PLUGIN_ABI_VERSION,
    kPluginId, "MegaCity / BioView", "0.1.0", kBackends,
    &create_instance, &quiesce_instance, &destroy_instance,
    &set_viewport, &set_visible, &set_focused, &handle_input,
    &tick, &render_vulkan, &render_metal, &query_extension };

} // namespace

extern "C" DRAXUL_PLUGIN_EXPORT const DraxulPluginApiV2*
draxul_plugin_query_v2(uint32_t requested_abi)
{
    return requested_abi == DRAXUL_PLUGIN_ABI_VERSION ? &kApi : nullptr;
}
