#include <draxul/plugin_host.h>

#include "plugin_render_pass.h"

#include <draxul/base_renderer.h>
#include <draxul/host_registry.h>
#include <draxul/imgui_host.h>
#include <draxul/legacy_product_plugin_services.h>
#include <draxul/log.h>
#include <draxul/plugin_manager.h>
#include <draxul/renderer.h>
#include <nlohmann/json.hpp>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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

std::filesystem::path environment_path(const char* name,
    std::filesystem::path fallback)
{
    const char* value = std::getenv(name);
    return value && *value ? std::filesystem::path(value) : std::move(fallback);
}

std::string path_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()),
        encoded.size());
}

bool valid_storage_key(std::string_view key)
{
    if (key.empty() || key.size() > 128 || key == "." || key == "..")
        return false;
    return std::all_of(key.begin(), key.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
    });
}

bool replace_file_atomically(const std::filesystem::path& temporary,
    const std::filesystem::path& target, std::error_code& error)
{
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    error = std::error_code(static_cast<int>(GetLastError()),
        std::system_category());
    return false;
#else
    std::filesystem::rename(temporary, target, error);
    return !error;
#endif
}

} // namespace

PluginHost::PluginHost(std::shared_ptr<PluginManager> manager,
    std::filesystem::path storage_root)
    : manager_(std::move(manager))
    , storage_root_override_(std::move(storage_root))
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
    pane_id_ = context.pane_id;
    main_thread_id_ = std::this_thread::get_id();
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
    initialize_service_paths();
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
        error_ = "Plugin instance creation failed for '" + plugin_id_
            + "'; check its configuration, bundled resources, and Draxul log";
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
    canvas2d_render_pass_ = nullptr;
    canvas2d_draw_data_ = nullptr;
    canvas2d_imgui_context_ = nullptr;
    plugin_imgui_draw_data_ = nullptr;
    plugin_imgui_context_ = nullptr;
    if (renderer_)
        renderer_->wait_idle();
    plugin_->api().destroy_instance(instance_);
    instance_ = nullptr;
    plugin_.reset();
    running_ = false;
    has_presentation_ = false;
    renderer_ = nullptr;
    imgui_host_ = nullptr;
}

void PluginHost::attach_imgui_host(IImGuiHost& host)
{
    imgui_host_ = &host;
    tick_pending_.store(true);
    if (auto* callbacks = callbacks_.load())
        callbacks->wake_window();
}

void PluginHost::set_imgui_font(const std::string& path,
    float size_pixels)
{
    imgui_font_path_ = path;
    imgui_font_size_pixels_ = size_pixels;
    tick_pending_.store(true);
    if (auto* callbacks = callbacks_.load())
        callbacks->wake_window();
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
    if (imgui_host_ && plugin_imgui_draw_data_ && plugin_imgui_context_)
    {
        imgui_host_->render_imgui_draw_data(
            static_cast<ImDrawData*>(plugin_imgui_draw_data_),
            static_cast<ImGuiContext*>(plugin_imgui_context_));
    }
    plugin_imgui_draw_data_ = nullptr;
    plugin_imgui_context_ = nullptr;
    if (canvas2d_render_pass_)
        frame.record_render_pass(*canvas2d_render_pass_, viewport);
    if (imgui_host_ && canvas2d_draw_data_ && canvas2d_imgui_context_)
    {
        imgui_host_->render_imgui_draw_data(
            static_cast<ImDrawData*>(canvas2d_draw_data_),
            static_cast<ImGuiContext*>(canvas2d_imgui_context_));
    }
    canvas2d_draw_data_ = nullptr;
    canvas2d_imgui_context_ = nullptr;
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

void PluginHost::initialize_service_paths()
{
    resource_path_ = std::filesystem::u8path(plugin_directory_);
    if (!storage_root_override_.empty())
    {
        config_path_ = storage_root_override_ / "config" / plugin_id_;
        data_path_ = storage_root_override_ / "data" / plugin_id_;
        cache_path_ = storage_root_override_ / "cache" / plugin_id_;
        temporary_path_ = storage_root_override_ / "temporary" / plugin_id_;
        return;
    }

#ifdef _WIN32
    const std::filesystem::path roaming = environment_path("APPDATA", ".");
    const std::filesystem::path local = environment_path("LOCALAPPDATA", roaming);
    config_path_ = roaming / "draxul" / "plugin-config" / plugin_id_;
    data_path_ = roaming / "draxul" / "plugin-data" / plugin_id_;
    cache_path_ = local / "draxul" / "cache" / "plugins" / plugin_id_;
#elif defined(__APPLE__)
    const std::filesystem::path home = environment_path("HOME", ".");
    const std::filesystem::path support
        = home / "Library" / "Application Support" / "draxul";
    config_path_ = support / "Plugin Config" / plugin_id_;
    data_path_ = support / "Plugins" / plugin_id_;
    cache_path_ = home / "Library" / "Caches" / "draxul" / "plugins" / plugin_id_;
#else
    const std::filesystem::path home = environment_path("HOME", ".");
    const std::filesystem::path config_root = environment_path(
        "XDG_CONFIG_HOME", home / ".config");
    const std::filesystem::path data_root = environment_path(
        "XDG_DATA_HOME", home / ".local" / "share");
    const std::filesystem::path cache_root = environment_path(
        "XDG_CACHE_HOME", home / ".cache");
    config_path_ = config_root / "draxul" / "plugins" / plugin_id_;
    data_path_ = data_root / "draxul" / "plugins" / plugin_id_;
    cache_path_ = cache_root / "draxul" / "plugins" / plugin_id_;
#endif
    std::error_code temp_error;
    temporary_path_ = std::filesystem::temp_directory_path(temp_error)
        / "draxul" / "plugins" / plugin_id_;
    if (temp_error)
        temporary_path_ = data_path_ / "temporary";
}

std::filesystem::path PluginHost::storage_path(uint32_t scope,
    std::string_view key) const
{
    if (scope == DRAXUL_PLUGIN_STORAGE_PLUGIN)
        return config_path_ / "state" / (std::string(key) + ".json");
    if (scope == DRAXUL_PLUGIN_STORAGE_PANE
        && valid_storage_key(pane_id_))
    {
        return config_path_ / "panes" / pane_id_
            / (std::string(key) + ".json");
    }
    return {};
}

int32_t PluginHost::query_service(void* context, const char* service_id,
    size_t service_id_length, uint32_t requested_version,
    void* service_table, size_t service_table_size)
{
    auto* host = static_cast<PluginHost*>(context);
    if (!host || !service_id || !service_table)
        return 0;
    const std::string_view id(service_id, service_id_length);
    if (id == DRAXUL_PLUGIN_PATH_SERVICE_ID
        && requested_version == DRAXUL_PLUGIN_PATH_SERVICE_VERSION
        && service_table_size >= sizeof(DraxulPluginPathServiceV2))
    {
        auto* service = static_cast<DraxulPluginPathServiceV2*>(service_table);
        *service = { sizeof(*service), DRAXUL_PLUGIN_PATH_SERVICE_VERSION,
            host, &PluginHost::get_service_path };
        return 1;
    }
    if (id == DRAXUL_PLUGIN_STORAGE_SERVICE_ID
        && requested_version == DRAXUL_PLUGIN_STORAGE_SERVICE_VERSION
        && service_table_size >= sizeof(DraxulPluginStorageServiceV2))
    {
        auto* service = static_cast<DraxulPluginStorageServiceV2*>(service_table);
        *service = { sizeof(*service), DRAXUL_PLUGIN_STORAGE_SERVICE_VERSION,
            host, &PluginHost::read_storage_json,
            &PluginHost::write_storage_json, &PluginHost::remove_storage };
        return 1;
    }
    if (id == DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_ID
        && requested_version == DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_VERSION
        && service_table_size >= sizeof(DraxulPluginImGuiOverlayServiceV2))
    {
        auto* service = static_cast<DraxulPluginImGuiOverlayServiceV2*>(service_table);
        *service = {
            sizeof(*service),
            DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_VERSION,
            IMGUI_VERSION_NUM,
            sizeof(ImDrawVert),
            sizeof(ImDrawIdx),
            host,
            &PluginHost::initialize_imgui_overlay,
            &PluginHost::shutdown_imgui_overlay,
            &PluginHost::rebuild_imgui_font_texture,
            &PluginHost::begin_imgui_frame,
            &PluginHost::render_imgui_draw_data,
            &PluginHost::get_imgui_font,
        };
        return 1;
    }
    if (id == DRAXUL_PLUGIN_CANVAS2D_SERVICE_ID
        && requested_version == DRAXUL_PLUGIN_CANVAS2D_SERVICE_VERSION
        && service_table_size >= sizeof(DraxulPluginCanvas2DServiceV2))
    {
        auto* service = static_cast<DraxulPluginCanvas2DServiceV2*>(service_table);
        *service = { sizeof(*service), DRAXUL_PLUGIN_CANVAS2D_SERVICE_VERSION,
            DRAXUL_PLUGIN_CANVAS2D_CPP_ABI_FINGERPRINT, host,
            &PluginHost::set_canvas2d_render_pass,
            &PluginHost::set_canvas2d_overlay };
        return 1;
    }
    return 0;
}

int32_t PluginHost::set_canvas2d_render_pass(void* context,
    void* render_pass, uint64_t fingerprint)
{
    auto* host = static_cast<PluginHost*>(context);
    if (!host || std::this_thread::get_id() != host->main_thread_id_
        || fingerprint != DRAXUL_PLUGIN_CANVAS2D_CPP_ABI_FINGERPRINT)
        return 0;
    host->canvas2d_render_pass_ = static_cast<IRenderPass*>(render_pass);
    return 1;
}

void PluginHost::set_canvas2d_overlay(void* context, void* draw_data,
    void* imgui_context)
{
    auto* host = static_cast<PluginHost*>(context);
    if (!host || std::this_thread::get_id() != host->main_thread_id_)
        return;
    host->canvas2d_draw_data_ = draw_data;
    host->canvas2d_imgui_context_ = imgui_context;
}

int32_t PluginHost::initialize_imgui_overlay(void* context,
    void* imgui_context)
{
    auto* host = static_cast<PluginHost*>(context);
    auto* target = static_cast<ImGuiContext*>(imgui_context);
    if (!host || !host->imgui_host_ || !target
        || std::this_thread::get_id() != host->main_thread_id_)
        return 0;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(target);
    const bool result = host->imgui_host_->initialize_imgui_backend();
    ImGui::SetCurrentContext(previous);
    return result ? 1 : 0;
}

void PluginHost::shutdown_imgui_overlay(void* context,
    void* imgui_context)
{
    auto* host = static_cast<PluginHost*>(context);
    auto* target = static_cast<ImGuiContext*>(imgui_context);
    if (!host || !host->imgui_host_ || !target
        || std::this_thread::get_id() != host->main_thread_id_)
        return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(target);
    host->imgui_host_->shutdown_imgui_backend();
    ImGui::SetCurrentContext(previous);
}

void PluginHost::rebuild_imgui_font_texture(void* context,
    void* imgui_context)
{
    auto* host = static_cast<PluginHost*>(context);
    auto* target = static_cast<ImGuiContext*>(imgui_context);
    if (!host || !host->imgui_host_ || !target
        || std::this_thread::get_id() != host->main_thread_id_)
        return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(target);
    host->imgui_host_->rebuild_imgui_font_texture();
    ImGui::SetCurrentContext(previous);
}

int32_t PluginHost::begin_imgui_frame(void* context,
    void* imgui_context)
{
    auto* host = static_cast<PluginHost*>(context);
    auto* target = static_cast<ImGuiContext*>(imgui_context);
    if (!host || !host->imgui_host_ || !target
        || std::this_thread::get_id() != host->main_thread_id_)
        return 0;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(target);
    host->imgui_host_->begin_imgui_frame();
    ImGui::SetCurrentContext(previous);
    return 1;
}

int32_t PluginHost::render_imgui_draw_data(void* context,
    void* draw_data, void* imgui_context)
{
    auto* host = static_cast<PluginHost*>(context);
    if (!host || !host->imgui_host_ || !draw_data || !imgui_context
        || std::this_thread::get_id() != host->main_thread_id_)
        return 0;
    host->plugin_imgui_draw_data_ = draw_data;
    host->plugin_imgui_context_ = imgui_context;
    return 1;
}

int32_t PluginHost::get_imgui_font(void* context, char* path,
    size_t* in_out_size, float* size_pixels)
{
    auto* host = static_cast<PluginHost*>(context);
    if (!host || !in_out_size || !size_pixels
        || std::this_thread::get_id() != host->main_thread_id_)
        return 0;
    *size_pixels = host->imgui_font_size_pixels_;
    const size_t required = host->imgui_font_path_.size() + 1;
    if (!path)
    {
        *in_out_size = required;
        return 1;
    }
    if (*in_out_size < required)
    {
        *in_out_size = required;
        return 0;
    }
    std::memcpy(path, host->imgui_font_path_.c_str(), required);
    *in_out_size = required;
    return 1;
}

int32_t PluginHost::get_service_path(void* context, uint32_t path_kind,
    char* buffer, size_t* in_out_size)
{
    auto* host = static_cast<PluginHost*>(context);
    if (!host || !in_out_size)
        return 0;
    const std::filesystem::path* selected = nullptr;
    switch (path_kind)
    {
    case DRAXUL_PLUGIN_PATH_RESOURCES:
        selected = &host->resource_path_;
        break;
    case DRAXUL_PLUGIN_PATH_CONFIG:
        selected = &host->config_path_;
        break;
    case DRAXUL_PLUGIN_PATH_DATA:
        selected = &host->data_path_;
        break;
    case DRAXUL_PLUGIN_PATH_CACHE:
        selected = &host->cache_path_;
        break;
    case DRAXUL_PLUGIN_PATH_TEMPORARY:
        selected = &host->temporary_path_;
        break;
    default:
        return 0;
    }
    if (path_kind != DRAXUL_PLUGIN_PATH_RESOURCES)
    {
        std::error_code error;
        std::filesystem::create_directories(*selected, error);
        if (error)
            return 0;
    }
    const std::string value = path_utf8(*selected);
    const size_t required = value.size() + 1;
    if (!buffer)
    {
        *in_out_size = required;
        return 1;
    }
    if (*in_out_size < required)
    {
        *in_out_size = required;
        return 0;
    }
    std::memcpy(buffer, value.c_str(), required);
    *in_out_size = required;
    return 1;
}

uint32_t PluginHost::read_storage_json(void* context, uint32_t scope,
    const char* key, size_t key_length, char* buffer, size_t* in_out_size)
{
    auto* host = static_cast<PluginHost*>(context);
    if (!host || std::this_thread::get_id() != host->main_thread_id_)
        return DRAXUL_PLUGIN_STORAGE_WRONG_THREAD;
    if (!key || !in_out_size
        || !valid_storage_key(std::string_view(key, key_length)))
        return DRAXUL_PLUGIN_STORAGE_INVALID_KEY;
    const auto path = host->storage_path(scope, std::string_view(key, key_length));
    if (path.empty())
        return DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE;
    std::error_code error;
    const auto file_size = std::filesystem::file_size(path, error);
    if (error)
    {
        return error == std::errc::no_such_file_or_directory
            ? DRAXUL_PLUGIN_STORAGE_NOT_FOUND
            : DRAXUL_PLUGIN_STORAGE_IO_ERROR;
    }
    if (file_size > DRAXUL_PLUGIN_MAX_STORAGE_JSON_BYTES)
        return DRAXUL_PLUGIN_STORAGE_TOO_LARGE;
    std::ifstream input(path, std::ios::binary);
    std::string value((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof())
        return DRAXUL_PLUGIN_STORAGE_IO_ERROR;
    try
    {
        (void)nlohmann::json::parse(value);
    }
    catch (...)
    {
        return DRAXUL_PLUGIN_STORAGE_INVALID_JSON;
    }
    const size_t required = value.size() + 1;
    if (!buffer)
    {
        *in_out_size = required;
        return DRAXUL_PLUGIN_STORAGE_OK;
    }
    if (*in_out_size < required)
    {
        *in_out_size = required;
        return DRAXUL_PLUGIN_STORAGE_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, value.c_str(), required);
    *in_out_size = required;
    return DRAXUL_PLUGIN_STORAGE_OK;
}

uint32_t PluginHost::write_storage_json(void* context, uint32_t scope,
    const char* key, size_t key_length, const char* json, size_t json_length)
{
    auto* host = static_cast<PluginHost*>(context);
    if (!host || std::this_thread::get_id() != host->main_thread_id_)
        return DRAXUL_PLUGIN_STORAGE_WRONG_THREAD;
    if (!key || !valid_storage_key(std::string_view(key, key_length)))
        return DRAXUL_PLUGIN_STORAGE_INVALID_KEY;
    if (!json)
        return DRAXUL_PLUGIN_STORAGE_INVALID_JSON;
    if (json_length > DRAXUL_PLUGIN_MAX_STORAGE_JSON_BYTES)
        return DRAXUL_PLUGIN_STORAGE_TOO_LARGE;
    try
    {
        (void)nlohmann::json::parse(json, json + json_length);
    }
    catch (...)
    {
        return DRAXUL_PLUGIN_STORAGE_INVALID_JSON;
    }
    const auto path = host->storage_path(scope, std::string_view(key, key_length));
    if (path.empty())
        return DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return DRAXUL_PLUGIN_STORAGE_IO_ERROR;
    static std::atomic<uint64_t> serial{ 0 };
    const auto temporary = path.parent_path()
        / (path.filename().string() + ".tmp-"
            + std::to_string(serial.fetch_add(1)));
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(json, static_cast<std::streamsize>(json_length));
        output.flush();
        if (!output)
        {
            output.close();
            std::filesystem::remove(temporary, error);
            return DRAXUL_PLUGIN_STORAGE_IO_ERROR;
        }
    }
    if (!replace_file_atomically(temporary, path, error))
    {
        std::filesystem::remove(temporary, error);
        return DRAXUL_PLUGIN_STORAGE_IO_ERROR;
    }
    return DRAXUL_PLUGIN_STORAGE_OK;
}

uint32_t PluginHost::remove_storage(void* context, uint32_t scope,
    const char* key, size_t key_length)
{
    auto* host = static_cast<PluginHost*>(context);
    if (!host || std::this_thread::get_id() != host->main_thread_id_)
        return DRAXUL_PLUGIN_STORAGE_WRONG_THREAD;
    if (!key || !valid_storage_key(std::string_view(key, key_length)))
        return DRAXUL_PLUGIN_STORAGE_INVALID_KEY;
    const auto path = host->storage_path(scope, std::string_view(key, key_length));
    if (path.empty())
        return DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE;
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    if (error)
        return DRAXUL_PLUGIN_STORAGE_IO_ERROR;
    return removed ? DRAXUL_PLUGIN_STORAGE_OK
                   : DRAXUL_PLUGIN_STORAGE_NOT_FOUND;
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
