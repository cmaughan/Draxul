#include <draxul/plugin_host.h>

#include "plugin_render_pass.h"

#include <draxul/base_renderer.h>
#include <draxul/host_registry.h>
#include <draxul/log.h>
#include <draxul/plugin_manager.h>
#include <draxul/renderer.h>

#include <algorithm>

namespace draxul
{

PluginHost::PluginHost(std::shared_ptr<PluginManager> manager)
    : manager_(std::move(manager))
{
}

PluginHost::~PluginHost()
{
    shutdown();
}

bool PluginHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_.store(&callbacks);
    renderer_ = context.grid_renderer;
    viewport_ = context.initial_viewport;
    plugin_id_ = context.launch_options.client_plugin_id;
    config_json_ = context.launch_options.client_plugin_config_json.empty()
        ? "{}" : context.launch_options.client_plugin_config_json;
    if (!manager_ || plugin_id_.empty())
    {
        error_ = plugin_id_.empty() ? "Plugin id is missing" : "Plugin discovery is unavailable";
        return false;
    }
    std::string load_error;
    plugin_ = manager_->load(plugin_id_, load_error);
    if (!plugin_)
    {
        error_ = std::move(load_error);
        return false;
    }
    plugin_directory_ = plugin_->manifest().directory.string();
    host_api_ = {
        .struct_size = sizeof(host_api_),
        .abi_version = DRAXUL_PLUGIN_ABI_V1,
        .host_context = this,
        .request_redraw = &PluginHost::request_redraw,
        .log = &PluginHost::log_message,
    };
    const DraxulPluginCreateInfoV1 create_info{
        .struct_size = sizeof(DraxulPluginCreateInfoV1),
        .host = &host_api_,
        .plugin_id = plugin_id_.c_str(),
        .plugin_directory_utf8 = plugin_directory_.c_str(),
        .config_json = config_json_.c_str(),
        .config_json_length = config_json_.size(),
        .initial_viewport = plugin_viewport(),
    };
    instance_ = plugin_->api().create_instance(&create_info);
    if (!instance_)
    {
        error_ = "Plugin instance creation failed";
        return false;
    }
    started_at_ = std::chrono::steady_clock::now();
    render_pass_ = create_plugin_render_pass(plugin_, instance_, *this, started_at_);
    if (!render_pass_)
    {
        error_ = "Plugin does not support this renderer backend";
        plugin_->api().destroy_instance(instance_);
        instance_ = nullptr;
        return false;
    }
    running_ = true;
    callbacks.request_frame();
    return true;
}

void PluginHost::shutdown()
{
    if (!instance_)
        return;
    render_pass_.reset();
    if (renderer_)
        renderer_->wait_idle();
    plugin_->api().destroy_instance(instance_);
    instance_ = nullptr;
    plugin_.reset();
    running_ = false;
    callbacks_.store(nullptr);
    renderer_ = nullptr;
}

DraxulPluginViewportV1 PluginHost::plugin_viewport() const
{
    return {
        .struct_size = sizeof(DraxulPluginViewportV1),
        .x = viewport_.pixel_pos.x,
        .y = viewport_.pixel_pos.y,
        .width = viewport_.pixel_size.x,
        .height = viewport_.pixel_size.y,
        .pixel_scale = viewport_.pixel_scale,
        .display_ppi = 96.0f * viewport_.pixel_scale,
    };
}

void PluginHost::set_viewport(const HostViewport& viewport)
{
    const bool changed = viewport.pixel_pos != viewport_.pixel_pos
        || viewport.pixel_size != viewport_.pixel_size
        || viewport.grid_size != viewport_.grid_size
        || viewport.padding != viewport_.padding
        || viewport.pixel_scale != viewport_.pixel_scale;
    if (!changed)
        return;
    viewport_ = viewport;
    if (instance_ && plugin_->api().set_viewport)
    {
        const auto value = plugin_viewport();
        plugin_->api().set_viewport(instance_, &value);
    }
    if (auto* callbacks = callbacks_.load())
        callbacks->request_frame();
}

void PluginHost::set_presentation_visible(bool visible)
{
    if (visible_ == visible)
        return;
    visible_ = visible;
    if (!visible)
        next_frame_.reset();
    if (instance_ && plugin_->api().set_visible)
        plugin_->api().set_visible(instance_, visible ? 1 : 0);
    if (visible)
    {
        if (auto* callbacks = callbacks_.load())
            callbacks->request_frame();
    }
}

void PluginHost::pump()
{
    bool frame_needed = redraw_pending_.exchange(false);
    if (visible_ && next_frame_
        && std::chrono::steady_clock::now() >= *next_frame_)
    {
        // A host deadline only wakes the application loop. Convert the
        // elapsed deadline into an explicit frame request, and consume it so
        // the loop cannot spin before the render callback supplies the next
        // animation deadline.
        next_frame_.reset();
        frame_needed = true;
    }
    if (frame_needed)
    {
        if (auto* callbacks = callbacks_.load())
            callbacks->request_frame();
    }
}

void PluginHost::draw(IFrameContext& frame)
{
    if (!visible_ || !render_pass_)
        return;
    RenderViewport viewport{
        viewport_.pixel_pos.x,
        viewport_.pixel_pos.y,
        viewport_.pixel_size.x,
        viewport_.pixel_size.y,
    };
    frame.record_render_pass(*render_pass_, viewport);
    frame.flush_submit_chunk();
}

std::optional<std::chrono::steady_clock::time_point> PluginHost::next_deadline() const
{
    return visible_ ? next_frame_ : std::nullopt;
}

void PluginHost::accept_render_result(const DraxulPluginRenderResultV1& result)
{
    if (!result.ok)
    {
        error_ = result.error_message ? result.error_message : "Plugin render failed";
        next_frame_.reset();
        return;
    }
    if (visible_ && result.next_frame_delay_ns != DRAXUL_PLUGIN_NO_DEADLINE)
    {
        next_frame_ = std::chrono::steady_clock::now()
            + std::chrono::nanoseconds(result.next_frame_delay_ns);
    }
    else
        next_frame_.reset();
}

void PluginHost::request_redraw(void* context)
{
    auto* host = static_cast<PluginHost*>(context);
    host->redraw_pending_.store(true);
    if (auto* callbacks = host->callbacks_.load())
        callbacks->wake_window();
}

void PluginHost::log_message(void*, uint32_t level, const char* message, size_t length)
{
    const std::string text(message ? message : "", message ? length : 0);
    const LogLevel mapped = level >= DRAXUL_PLUGIN_LOG_ERROR ? LogLevel::Error
        : level == DRAXUL_PLUGIN_LOG_WARNING ? LogLevel::Warn
        : level == DRAXUL_PLUGIN_LOG_INFO ? LogLevel::Info : LogLevel::Debug;
    log_printf(mapped, LogCategory::Renderer, "Plugin: %s", text.c_str());
}

void PluginHost::send_input(DraxulPluginInputEventV1 event)
{
    if (instance_ && plugin_->api().handle_input)
        plugin_->api().handle_input(instance_, &event);
}

void PluginHost::send_focus(bool focused)
{
    if (instance_ && plugin_->api().set_focused)
        plugin_->api().set_focused(instance_, focused ? 1 : 0);
    DraxulPluginInputEventV1 event{};
    event.struct_size = sizeof(event);
    event.kind = DRAXUL_PLUGIN_INPUT_FOCUS;
    event.pressed = focused ? 1 : 0;
    send_input(event);
}

void PluginHost::on_focus_gained() { send_focus(true); }
void PluginHost::on_focus_lost() { send_focus(false); }

void PluginHost::on_key(const KeyEvent& source)
{
    DraxulPluginInputEventV1 event{};
    event.struct_size = sizeof(event);
    event.kind = DRAXUL_PLUGIN_INPUT_KEY;
    event.modifiers = source.mod;
    event.physical_key = static_cast<uint32_t>(source.scancode);
    event.logical_key = source.keycode;
    event.pressed = source.pressed ? 1 : 0;
    send_input(event);
}

void PluginHost::on_text_input(const TextInputEvent& source)
{
    DraxulPluginInputEventV1 event{};
    event.struct_size = sizeof(event);
    event.kind = DRAXUL_PLUGIN_INPUT_TEXT;
    event.text_utf8 = source.text.data();
    event.text_length = source.text.size();
    send_input(event);
}

void PluginHost::on_text_editing(const TextEditingEvent& source)
{
    DraxulPluginInputEventV1 event{};
    event.struct_size = sizeof(event);
    event.kind = DRAXUL_PLUGIN_INPUT_COMPOSITION;
    event.text_utf8 = source.text.data();
    event.text_length = source.text.size();
    event.composition_start = source.start;
    event.composition_length = source.length;
    send_input(event);
}

void PluginHost::on_mouse_button(const MouseButtonEvent& source)
{
    DraxulPluginInputEventV1 event{};
    event.struct_size = sizeof(event);
    event.kind = DRAXUL_PLUGIN_INPUT_POINTER_BUTTON;
    event.modifiers = source.mod;
    event.button = source.button;
    event.pressed = source.pressed ? 1 : 0;
    event.clicks = source.clicks;
    event.x = source.pos.x - viewport_.pixel_pos.x;
    event.y = source.pos.y - viewport_.pixel_pos.y;
    send_input(event);
}

void PluginHost::on_mouse_move(const MouseMoveEvent& source)
{
    DraxulPluginInputEventV1 event{};
    event.struct_size = sizeof(event);
    event.kind = DRAXUL_PLUGIN_INPUT_POINTER_MOVE;
    event.modifiers = source.mod;
    event.x = source.pos.x - viewport_.pixel_pos.x;
    event.y = source.pos.y - viewport_.pixel_pos.y;
    event.delta_x = source.delta.x;
    event.delta_y = source.delta.y;
    event.buttons = source.buttons;
    send_input(event);
}

void PluginHost::on_mouse_wheel(const MouseWheelEvent& source)
{
    DraxulPluginInputEventV1 event{};
    event.struct_size = sizeof(event);
    event.kind = DRAXUL_PLUGIN_INPUT_WHEEL;
    event.modifiers = source.mod;
    event.x = source.pos.x - viewport_.pixel_pos.x;
    event.y = source.pos.y - viewport_.pixel_pos.y;
    event.delta_x = source.delta.x;
    event.delta_y = source.delta.y;
    send_input(event);
}

std::string PluginHost::display_name() const
{
    return plugin_ && plugin_->api().display_name
        ? plugin_->api().display_name : plugin_id_;
}

HostRuntimeState PluginHost::runtime_state() const
{
    return { .content_ready = running_ && error_.empty() };
}

HostDebugState PluginHost::debug_state() const
{
    return { .name = display_name() };
}

void register_plugin_host_provider(HostProviderRegistry& registry,
    std::shared_ptr<PluginManager> manager)
{
    registry.register_provider(HostKind::Plugin,
        [manager = std::move(manager)] { return std::make_unique<PluginHost>(manager); });
}

} // namespace draxul
