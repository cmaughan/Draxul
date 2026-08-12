#include <draxul/plugin_host.h>

#include "plugin_render_pass.h"

#include <draxul/base_renderer.h>
#include <draxul/host_registry.h>
#include <draxul/log.h>
#include <draxul/plugin_manager.h>
#include <draxul/renderer.h>

#include <algorithm>
#include <cstring>

namespace draxul
{
namespace
{

std::string copy_string(DraxulPluginStringViewV2 value)
{
    return value.data ? std::string(value.data, value.length) : std::string{};
}

std::optional<MouseCursor> map_mouse_cursor(uint32_t cursor)
{
    switch (cursor)
    {
    case DRAXUL_PLUGIN_CURSOR_DEFAULT:
        return MouseCursor::Default;
    case DRAXUL_PLUGIN_CURSOR_RESIZE_LEFT_RIGHT:
        return MouseCursor::ResizeLeftRight;
    case DRAXUL_PLUGIN_CURSOR_RESIZE_UP_DOWN:
        return MouseCursor::ResizeUpDown;
    case DRAXUL_PLUGIN_CURSOR_POINTER:
        return MouseCursor::Pointer;
    default:
        return std::nullopt;
    }
}

} // namespace

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
    shutting_down_.store(false);
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
    started_at_ = std::chrono::steady_clock::now();
    host_api_ = {
        .struct_size = sizeof(host_api_),
        .abi_version = DRAXUL_PLUGIN_ABI_VERSION,
        .host_context = this,
        .request_redraw = &PluginHost::request_redraw,
        .request_tick = &PluginHost::request_tick,
        .notify_presentation_changed = &PluginHost::notify_presentation_changed,
        .log = &PluginHost::log_message,
        .query_service = &PluginHost::query_service,
    };
    const DraxulPluginCreateInfoV2 create_info{
        .struct_size = sizeof(DraxulPluginCreateInfoV2),
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

    if (plugin_->api().query_extension)
    {
        presentation_ = {};
        presentation_.struct_size = sizeof(presentation_);
        presentation_.extension_version = DRAXUL_PLUGIN_PRESENTATION_EXTENSION_VERSION;
        has_presentation_ = plugin_->api().query_extension(instance_,
            DRAXUL_PLUGIN_PRESENTATION_EXTENSION_ID,
            std::strlen(DRAXUL_PLUGIN_PRESENTATION_EXTENSION_ID),
            DRAXUL_PLUGIN_PRESENTATION_EXTENSION_VERSION,
            &presentation_, sizeof(presentation_))
            && presentation_.struct_size >= sizeof(presentation_)
            && presentation_.extension_version == DRAXUL_PLUGIN_PRESENTATION_EXTENSION_VERSION
            && presentation_.get_state;
    }

    render_pass_ = create_plugin_render_pass(plugin_, instance_, *this, started_at_);
    if (!render_pass_)
    {
        error_ = "Plugin does not support this renderer backend";
        if (plugin_->api().quiesce_instance)
            plugin_->api().quiesce_instance(instance_);
        plugin_->api().destroy_instance(instance_);
        instance_ = nullptr;
        return false;
    }
    running_ = true;
    if (plugin_->api().tick)
        next_tick_ = started_at_;
    callbacks.request_frame();
    return true;
}

void PluginHost::shutdown()
{
    if (!instance_)
    {
        callbacks_.store(nullptr);
        return;
    }
    shutting_down_.store(true);
    callbacks_.store(nullptr);
    next_frame_.reset();
    next_tick_.reset();
    if (plugin_->api().quiesce_instance)
        plugin_->api().quiesce_instance(instance_);
    render_pass_.reset();
    if (renderer_)
        renderer_->wait_idle();
    plugin_->api().destroy_instance(instance_);
    instance_ = nullptr;
    plugin_.reset();
    running_ = false;
    has_presentation_ = false;
    renderer_ = nullptr;
}

DraxulPluginViewportV2 PluginHost::plugin_viewport() const
{
    return {
        .struct_size = sizeof(DraxulPluginViewportV2),
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
    if (instance_ && plugin_->api().tick)
        next_tick_ = std::chrono::steady_clock::now();
    if (visible)
    {
        if (auto* callbacks = callbacks_.load())
            callbacks->request_frame();
    }
}

void PluginHost::run_tick(std::chrono::steady_clock::time_point now,
    bool& frame_needed)
{
    if (!instance_ || !plugin_->api().tick)
        return;
    DraxulPluginTickInfoV2 info{
        .struct_size = sizeof(info),
        .monotonic_seconds = std::chrono::duration<double>(now - started_at_).count(),
        .visible = visible_ ? 1 : 0,
        .focused = focused_ ? 1 : 0,
    };
    const DraxulPluginTickResultV2 result = plugin_->api().tick(instance_, &info);
    if (!result.ok)
    {
        error_ = result.error_message ? result.error_message : "Plugin tick failed";
        next_tick_.reset();
        return;
    }
    if (result.request_redraw && visible_)
        frame_needed = true;
    if (result.next_tick_delay_ns != DRAXUL_PLUGIN_NO_DEADLINE)
        next_tick_ = now + std::chrono::nanoseconds(result.next_tick_delay_ns);
    else
        next_tick_.reset();
}

void PluginHost::pump()
{
    const auto now = std::chrono::steady_clock::now();
    bool frame_needed = redraw_pending_.exchange(false)
        || presentation_pending_.exchange(false);
    const bool tick_due = tick_pending_.exchange(false)
        || (next_tick_ && now >= *next_tick_);
    if (tick_due)
    {
        next_tick_.reset();
        run_tick(now, frame_needed);
    }
    if (visible_ && next_frame_ && now >= *next_frame_)
    {
        // A render deadline only wakes the application loop. Consume it so the
        // loop cannot spin before the render callback supplies another one.
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
    std::optional<std::chrono::steady_clock::time_point> result = next_tick_;
    if (visible_ && next_frame_ && (!result || *next_frame_ < *result))
        result = next_frame_;
    return result;
}

void PluginHost::accept_render_result(const DraxulPluginRenderResultV2& result)
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
    if (!host || host->shutting_down_.load())
        return;
    host->redraw_pending_.store(true);
    if (auto* callbacks = host->callbacks_.load())
        callbacks->wake_window();
}

void PluginHost::request_tick(void* context)
{
    auto* host = static_cast<PluginHost*>(context);
    if (!host || host->shutting_down_.load())
        return;
    host->tick_pending_.store(true);
    if (auto* callbacks = host->callbacks_.load())
        callbacks->wake_window();
}

void PluginHost::notify_presentation_changed(void* context)
{
    auto* host = static_cast<PluginHost*>(context);
    if (!host || host->shutting_down_.load())
        return;
    host->presentation_pending_.store(true);
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

int32_t PluginHost::query_service(void*, const char*, size_t, uint32_t,
    void*, size_t)
{
    // Slice 1 establishes negotiation. Concrete host services are added by
    // their consuming vertical slices.
    return 0;
}

void PluginHost::send_input(DraxulPluginInputEventV2 event)
{
    if (instance_ && plugin_->api().handle_input)
        plugin_->api().handle_input(instance_, &event);
}

void PluginHost::send_focus(bool focused)
{
    focused_ = focused;
    if (instance_ && plugin_->api().set_focused)
        plugin_->api().set_focused(instance_, focused ? 1 : 0);
    DraxulPluginInputEventV2 event{};
    event.struct_size = sizeof(event);
    event.kind = DRAXUL_PLUGIN_INPUT_FOCUS;
    event.pressed = focused ? 1 : 0;
    send_input(event);
}

void PluginHost::on_focus_gained() { send_focus(true); }
void PluginHost::on_focus_lost() { send_focus(false); }

void PluginHost::on_key(const KeyEvent& source)
{
    DraxulPluginInputEventV2 event{};
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
    DraxulPluginInputEventV2 event{};
    event.struct_size = sizeof(event);
    event.kind = DRAXUL_PLUGIN_INPUT_TEXT;
    event.text_utf8 = source.text.data();
    event.text_length = source.text.size();
    send_input(event);
}

void PluginHost::on_text_editing(const TextEditingEvent& source)
{
    DraxulPluginInputEventV2 event{};
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
    DraxulPluginInputEventV2 event{};
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
    DraxulPluginInputEventV2 event{};
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
    DraxulPluginInputEventV2 event{};
    event.struct_size = sizeof(event);
    event.kind = DRAXUL_PLUGIN_INPUT_WHEEL;
    event.modifiers = source.mod;
    event.x = source.pos.x - viewport_.pixel_pos.x;
    event.y = source.pos.y - viewport_.pixel_pos.y;
    event.delta_x = source.delta.x;
    event.delta_y = source.delta.y;
    send_input(event);
}

std::optional<PluginHost::PresentationSnapshot> PluginHost::presentation_snapshot() const
{
    if (!instance_ || !has_presentation_ || !presentation_.get_state)
        return std::nullopt;
    DraxulPluginPresentationStateV2 state{};
    state.struct_size = sizeof(state);
    if (!presentation_.get_state(instance_, &state)
        || state.struct_size < sizeof(state))
        return std::nullopt;
    PresentationSnapshot result;
    result.display_name = copy_string(state.display_name);
    result.status_text = copy_string(state.status_text);
    result.background = { state.background_red, state.background_green,
        state.background_blue, state.background_alpha };
    result.content_ready = state.content_ready != 0;
    result.mouse_cursor = state.mouse_cursor;
    result.print_hint.content_pos = {
        state.print_hint.content_x, state.print_hint.content_y };
    result.print_hint.content_size = {
        state.print_hint.content_width, state.print_hint.content_height };
    result.print_hint.paper_white = state.print_hint.paper_white != 0;
    return result;
}

std::optional<MouseCursor> PluginHost::mouse_cursor_at(int, int) const
{
    const auto state = presentation_snapshot();
    return state ? map_mouse_cursor(state->mouse_cursor) : std::nullopt;
}

bool PluginHost::dispatch_action(std::string_view action)
{
    if (!instance_ || !has_presentation_ || !presentation_.dispatch_action)
        return false;
    return presentation_.dispatch_action(instance_, action.data(), action.size()) != 0;
}

std::string PluginHost::display_name() const
{
    if (const auto state = presentation_snapshot(); state && !state->display_name.empty())
        return state->display_name;
    return plugin_ && plugin_->api().display_name
        ? plugin_->api().display_name : plugin_id_;
}

std::string PluginHost::status_text() const
{
    if (!error_.empty())
        return error_;
    if (const auto state = presentation_snapshot())
        return state->status_text;
    return {};
}

Color PluginHost::default_background() const
{
    if (const auto state = presentation_snapshot())
        return state->background;
    return { 0.04f, 0.05f, 0.08f, 1.0f };
}

HostRuntimeState PluginHost::runtime_state() const
{
    const auto state = presentation_snapshot();
    return { .content_ready = running_ && error_.empty()
            && (!state || state->content_ready) };
}

HostDebugState PluginHost::debug_state() const
{
    return { .name = display_name() };
}

HostPrintHint PluginHost::print_hint() const
{
    if (const auto state = presentation_snapshot())
        return state->print_hint;
    return {};
}

void register_plugin_host_provider(HostProviderRegistry& registry,
    std::shared_ptr<PluginManager> manager)
{
    registry.register_provider(HostKind::Plugin,
        [manager = std::move(manager)] { return std::make_unique<PluginHost>(manager); });
}

} // namespace draxul
