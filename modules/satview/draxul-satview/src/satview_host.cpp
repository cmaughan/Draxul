#include <draxul/satview/satview_host.h>

#include "satview_scene_pass.h"
#include "satview_simulation_worker.h"

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <cmath>
#include <draxul/host_registry.h>
#include <draxul/imgui_host.h>
#include <draxul/log.h>
#include <draxul/sdl_imgui_input.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <string_view>
#include <utility>
#include <vector>

namespace draxul::satview
{

namespace
{

constexpr auto kFrameTick = std::chrono::milliseconds(33);
constexpr std::size_t kDefaultTrackSatelliteLimit = 256;
constexpr std::size_t kDefaultTrackSampleCount = 48;
constexpr float kControlPanelDefaultWidth = 430.0f;
constexpr float kControlPanelDefaultHeight = 500.0f;
constexpr float kControlPanelMinWidth = 380.0f;
constexpr float kControlPanelMinHeight = 360.0f;
constexpr float kControlMinWidgetWidth = 96.0f;
constexpr int kClickSelectionMaxDistancePixels = 18;
constexpr int kClickDragSlopPixels = 5;

double unix_seconds_now()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

glm::vec3 camera_position(float yaw, float pitch, float distance)
{
    const float cp = std::cos(pitch);
    return glm::vec3(
        distance * cp * std::sin(yaw),
        distance * std::sin(pitch),
        distance * cp * std::cos(yaw));
}

glm::vec3 sun_direction(double seconds)
{
    // First slice: approximate a moving sun direction in the equatorial plane.
    // Later satellite work will replace this with a UTC astronomy model.
    const double day_fraction = std::fmod(seconds / 86400.0, 1.0);
    const float angle = static_cast<float>(day_fraction * glm::two_pi<double>());
    return glm::normalize(glm::vec3(std::cos(angle), 0.18f, std::sin(angle)));
}

glm::vec4 orbit_class_color(OrbitClass orbit_class, float alpha)
{
    switch (orbit_class)
    {
    case OrbitClass::LowEarth:
        return glm::vec4(0.18f, 0.78f, 1.00f, alpha);
    case OrbitClass::MediumEarth:
        return glm::vec4(0.46f, 0.92f, 0.42f, alpha);
    case OrbitClass::Geosynchronous:
        return glm::vec4(1.00f, 0.72f, 0.24f, alpha);
    case OrbitClass::HighlyElliptical:
        return glm::vec4(0.96f, 0.42f, 0.90f, alpha);
    case OrbitClass::Other:
        return glm::vec4(0.72f, 0.78f, 0.86f, alpha);
    }
    return glm::vec4(0.72f, 0.78f, 0.86f, alpha);
}

glm::vec4 object_kind_color(SatelliteObjectKind kind, float alpha)
{
    switch (kind)
    {
    case SatelliteObjectKind::Payload:
        return glm::vec4(0.12f, 0.86f, 0.98f, alpha);
    case SatelliteObjectKind::RocketBody:
        return glm::vec4(1.00f, 0.54f, 0.18f, alpha);
    case SatelliteObjectKind::Debris:
        return glm::vec4(1.00f, 0.28f, 0.36f, alpha);
    case SatelliteObjectKind::Unknown:
        return glm::vec4(0.72f, 0.78f, 0.86f, alpha);
    }
    return glm::vec4(0.72f, 0.78f, 0.86f, alpha);
}

glm::vec4 satellite_color(OrbitClass orbit_class, SatelliteObjectKind object_kind, SatViewColorMode color_mode, float alpha)
{
    return color_mode == SatViewColorMode::ObjectType
        ? object_kind_color(object_kind, alpha)
        : orbit_class_color(orbit_class, alpha);
}

glm::vec3 to_vec3(const glm::dvec3& value)
{
    return glm::vec3(
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z));
}

void append_line(std::vector<SatViewSceneVertex>& vertices, glm::vec3 a, glm::vec3 b, glm::vec4 color)
{
    vertices.push_back({ glm::vec4(a, 1.0f), color });
    vertices.push_back({ glm::vec4(b, 1.0f), color });
}

bool satellite_visible(
    const SatViewFilterState& filter,
    const SatelliteOrbitTrack& track,
    std::string_view source_label)
{
    return satview_filter_matches(filter, make_satview_filter_candidate(track, source_label));
}

bool satellite_visible(
    const SatViewFilterState& filter,
    const SatellitePropagatedState& state,
    std::string_view source_label)
{
    return satview_filter_matches(filter, make_satview_filter_candidate(state, source_label));
}

void append_track_vertices(
    std::vector<SatViewSceneVertex>& vertices,
    const SatViewSimulationSnapshot& snapshot,
    const SatViewFilterState& filter,
    std::string_view source_label,
    std::optional<std::int64_t> selected_id,
    SatViewTrackDisplayMode track_display_mode,
    SatViewColorMode color_mode)
{
    if (!snapshot.tracks)
        return;
    for (const SatelliteOrbitTrack& track : *snapshot.tracks)
    {
        if (track_display_mode == SatViewTrackDisplayMode::SelectedOnly
            && (!selected_id.has_value() || track.norad_catalog_id != *selected_id))
        {
            continue;
        }

        if (!satellite_visible(filter, track, source_label))
            continue;

        const auto& points = track.render_teme_points_earth_radii;
        if (points.size() < 2)
            continue;

        const bool selected = selected_id.has_value() && track.norad_catalog_id == *selected_id;
        const glm::vec4 color = selected
            ? glm::mix(satellite_color(track.orbit_class, track.object_kind, color_mode, 0.98f), glm::vec4(1.0f), 0.38f)
            : satellite_color(track.orbit_class, track.object_kind, color_mode, 0.62f);
        for (std::size_t i = 1; i < points.size(); ++i)
            append_line(vertices, to_vec3(points[i - 1]), to_vec3(points[i]), color);
        append_line(vertices, to_vec3(points.back()), to_vec3(points.front()), color * glm::vec4(1.0f, 1.0f, 1.0f, 0.82f));
    }
}

double render_simulation_seconds(const SatViewSimulationSnapshot& snapshot)
{
    if (snapshot.paused)
        return snapshot.simulation_seconds;

    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - snapshot.produced_at)
        .count();
    return snapshot.simulation_seconds + elapsed * static_cast<double>(snapshot.time_speed);
}

glm::dvec3 interpolated_teme_position(
    const SatViewSimulationSnapshot& snapshot,
    std::size_t state_index,
    double render_seconds)
{
    if (state_index >= snapshot.states.size()
        || state_index >= snapshot.next_teme_positions_km.size()
        || snapshot.next_simulation_seconds <= snapshot.simulation_seconds)
    {
        return snapshot.states[state_index].teme_position_km;
    }

    const double span = snapshot.next_simulation_seconds - snapshot.simulation_seconds;
    const double alpha = std::clamp((render_seconds - snapshot.simulation_seconds) / span, 0.0, 1.0);
    return glm::mix(snapshot.states[state_index].teme_position_km, snapshot.next_teme_positions_km[state_index], alpha);
}

void append_marker_vertices(
    std::vector<SatViewSceneVertex>& vertices,
    const SatViewSimulationSnapshot& snapshot,
    const SatViewFilterState& filter,
    std::string_view source_label,
    std::optional<std::int64_t> selected_id,
    SatViewColorMode color_mode,
    std::size_t marker_limit,
    double render_seconds,
    glm::vec3 camera_right,
    glm::vec3 camera_up)
{
    std::size_t visible_marker_index = 0;
    for (std::size_t state_index = 0; state_index < snapshot.states.size(); ++state_index)
    {
        const SatellitePropagatedState& state = snapshot.states[state_index];
        if (!satellite_visible(filter, state, source_label))
            continue;

        const bool selected = selected_id.has_value() && state.norad_catalog_id == *selected_id;
        const bool under_marker_limit = marker_limit == 0 || visible_marker_index < marker_limit;
        ++visible_marker_index;
        if (!under_marker_limit && !selected)
            continue;

        const glm::vec3 center = to_vec3(
            interpolated_teme_position(snapshot, state_index, render_seconds) / kSatViewEarthEquatorialRadiusKm);
        const float range = glm::length(center);
        const float base_size = std::clamp(0.006f + range * 0.0022f, 0.008f, 0.026f);
        const float size = selected ? base_size * 2.2f : base_size;
        const glm::vec4 color = selected
            ? glm::vec4(1.0f, 0.96f, 0.68f, 1.0f)
            : glm::mix(
                satellite_color(state.orbit_class, state.object_kind, color_mode, 0.95f),
                glm::vec4(1.0f),
                0.18f);

        append_line(vertices, center - camera_right * size, center + camera_right * size, color);
        append_line(vertices, center - camera_up * size, center + camera_up * size, color);
        if (selected)
        {
            const glm::vec3 diagonal_a = glm::normalize(camera_right + camera_up);
            const glm::vec3 diagonal_b = glm::normalize(camera_right - camera_up);
            append_line(vertices, center - diagonal_a * size, center + diagonal_a * size, color);
            append_line(vertices, center - diagonal_b * size, center + diagonal_b * size, color);
        }
    }
}

float earth_rotation(double seconds)
{
    const double day_fraction = std::fmod(seconds / 86164.0905, 1.0);
    return static_cast<float>(day_fraction * glm::two_pi<double>());
}

float control_widget_width(const char* label)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float available = ImGui::GetContentRegionAvail().x;
    const float label_width = ImGui::CalcTextSize(label).x;
    const float row_spacing = style.ItemInnerSpacing.x;
    return std::max(kControlMinWidgetWidth, available - label_width - row_spacing);
}

} // namespace

SatViewHost::SatViewHost() = default;

SatViewHost::~SatViewHost()
{
    shutdown();
}

bool SatViewHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    viewport_ = context.initial_viewport;
    show_ui_panel_ = context.launch_options.show_host_ui_panels;
    continuous_refresh_enabled_ = context.launch_options.request_continuous_refresh;
    IMGUI_CHECKVERSION();
    imgui_context_ = ImGui::CreateContext();
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
        io.ConfigWindowsResizeFromEdges = true;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImGui::StyleColorsDark();
    }
    catalog_service_.start();
    simulated_seconds_ = unix_seconds_now();
    last_draw_simulation_seconds_ = simulated_seconds_;
    const glm::vec3 sun = sun_direction(simulated_seconds_);
    yaw_ = std::atan2(sun.x, sun.z) + 0.65f;
    pitch_ = 0.25f;
    distance_ = 3.6f;
    track_satellite_limit_ = kDefaultTrackSatelliteLimit;
    track_sample_count_ = kDefaultTrackSampleCount;
    last_pump_time_ = std::chrono::steady_clock::now();
    last_activity_time_ = last_pump_time_;
    scene_pass_ = std::make_shared<SatViewScenePass>();
    SatViewSimulationControls simulation_controls;
    simulation_controls.time_speed = time_speed_;
    simulation_controls.paused = paused_;
    simulation_controls.track_satellite_limit = track_satellite_limit_;
    simulation_controls.track_sample_count = track_sample_count_;
    simulation_controls.selected_track_norad_catalog_id = selected_norad_catalog_id_;
    simulation_worker_ = std::make_unique<SatViewSimulationWorker>();
    simulation_worker_->start(simulated_seconds_, simulation_controls);
    running_ = true;
    callbacks.set_window_title("SatView");
    request_redraw();
    return true;
}

void SatViewHost::shutdown()
{
    catalog_service_.stop();
    if (simulation_worker_)
        simulation_worker_->stop();
    running_ = false;
    dragging_ = false;
    pending_click_ = false;
    scene_pass_.reset();
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        if (imgui_backend_)
            imgui_backend_->shutdown_imgui_backend();
        ImGui::DestroyContext(imgui_context_);
        imgui_context_ = nullptr;
        imgui_backend_ = nullptr;
    }
}

bool SatViewHost::is_running() const
{
    return running_;
}

std::string SatViewHost::init_error() const
{
    return init_error_;
}

void SatViewHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
    request_redraw();
}

void SatViewHost::pump()
{
    if (!running_)
        return;

    catalog_service_.pump();
    const std::uint64_t catalog_generation = catalog_service_.catalog_generation();
    if (simulation_worker_ && catalog_generation != 0 && catalog_generation != simulation_catalog_generation_)
    {
        simulation_catalog_generation_ = catalog_generation;
        simulation_worker_->set_catalog(catalog_service_.catalog(), catalog_generation);
    }
    if (simulation_settings_dirty_)
    {
        sync_simulation_render_settings();
        simulation_settings_dirty_ = false;
    }
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last_pump_time_).count();
    last_pump_time_ = now;
    last_imgui_delta_seconds_ = dt;
    request_redraw();
}

void SatViewHost::draw(IFrameContext& frame)
{
    if (!scene_pass_)
        return;

    auto snapshot_guard = simulation_worker_ ? simulation_worker_->acquire_latest() : SatViewSnapshotExchange::ReadGuard{};
    const SatViewSimulationSnapshot* snapshot = snapshot_guard.get();
    const double simulation_seconds = snapshot ? render_simulation_seconds(*snapshot) : last_draw_simulation_seconds_;
    last_draw_simulation_seconds_ = simulation_seconds;

    const int pixel_w = std::max(1, viewport_.pixel_size.x);
    const int pixel_h = std::max(1, viewport_.pixel_size.y);
    const float aspect = static_cast<float>(pixel_w) / static_cast<float>(pixel_h);
    const glm::vec3 eye = camera_position(yaw_, pitch_, distance_);
    const glm::vec3 camera_forward = glm::normalize(-eye);
    const glm::vec3 camera_right = glm::normalize(glm::cross(camera_forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 camera_up = glm::normalize(glm::cross(camera_right, camera_forward));
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(42.0f), aspect, 0.05f, 64.0f);

    SatViewFrameUniforms uniforms;
    uniforms.view_proj = proj * view;
    uniforms.camera_pos = glm::vec4(eye, 1.0f);
    const glm::vec3 sun = sun_direction(simulation_seconds);
    uniforms.sun_dir_time = glm::vec4(sun, static_cast<float>(std::fmod(simulation_seconds, 86400.0)));
    uniforms.render_params = glm::vec4(
        static_cast<float>(kSatViewSphereLatitudeBands),
        static_cast<float>(kSatViewSphereLongitudeBands),
        earth_rotation(simulation_seconds),
        paused_ ? 1.0f : 0.0f);
    scene_pass_->set_frame(uniforms);

    std::vector<SatViewSceneVertex> scene_vertices;
    if (snapshot)
    {
        const std::size_t track_count = snapshot->tracks ? snapshot->tracks->size() : 0;
        scene_vertices.reserve(track_count * track_sample_count_ * 2 + snapshot->states.size() * 4);
        append_track_vertices(
            scene_vertices,
            *snapshot,
            filter_,
            snapshot->source_label,
            selected_norad_catalog_id_,
            track_display_mode_,
            color_mode_);
        append_marker_vertices(
            scene_vertices,
            *snapshot,
            filter_,
            snapshot->source_label,
            selected_norad_catalog_id_,
            color_mode_,
            marker_satellite_limit_,
            simulation_seconds,
            camera_right,
            camera_up);
    }
    scene_pass_->set_scene_vertices(scene_vertices);

    RenderViewport viewport;
    viewport.x = viewport_.pixel_pos.x;
    viewport.y = viewport_.pixel_pos.y;
    viewport.width = pixel_w;
    viewport.height = pixel_h;
    frame.record_render_pass(*scene_pass_, viewport);
    render_host_imgui(last_imgui_delta_seconds_, snapshot);
    if (imgui_context_ && imgui_backend_)
        frame.render_imgui(ImGui::GetDrawData(), imgui_context_);
    frame.flush_submit_chunk();
}

std::optional<std::chrono::steady_clock::time_point> SatViewHost::next_deadline() const
{
    if (!running_)
        return std::nullopt;
    if (continuous_refresh_enabled_)
        return std::chrono::steady_clock::now();
    return std::chrono::steady_clock::now() + kFrameTick;
}

void SatViewHost::on_mouse_button(const MouseButtonEvent& event)
{
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        int button = -1;
        switch (event.button)
        {
        case 1:
            button = 0;
            break;
        case 2:
            button = 2;
            break;
        case 3:
            button = 1;
            break;
        default:
            break;
        }
        if (button >= 0)
            ImGui::GetIO().AddMouseButtonEvent(button, event.pressed);

        if (ImGui::GetIO().WantCaptureMouse)
        {
            if (event.button == SDL_BUTTON_LEFT && !event.pressed)
            {
                dragging_ = false;
                pending_click_ = false;
            }
            request_redraw();
            return;
        }
    }

    if (event.button != SDL_BUTTON_LEFT)
        return;
    dragging_ = event.pressed;
    if (event.pressed)
    {
        pending_click_ = true;
        click_start_pos_ = event.pos;
    }
    else
    {
        if (pending_click_)
        {
            const glm::ivec2 delta = event.pos - click_start_pos_;
            const int distance_sq = delta.x * delta.x + delta.y * delta.y;
            if (distance_sq <= kClickDragSlopPixels * kClickDragSlopPixels)
                select_nearest_satellite(event.pos);
        }
        pending_click_ = false;
    }
    last_activity_time_ = std::chrono::steady_clock::now();
}

void SatViewHost::on_mouse_move(const MouseMoveEvent& event)
{
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGui::GetIO().AddMousePosEvent(static_cast<float>(event.pos.x), static_cast<float>(event.pos.y));
        if (ImGui::GetIO().WantCaptureMouse)
        {
            request_redraw();
            return;
        }
    }

    if (pending_click_)
    {
        const glm::ivec2 delta = event.pos - click_start_pos_;
        const int distance_sq = delta.x * delta.x + delta.y * delta.y;
        if (distance_sq > kClickDragSlopPixels * kClickDragSlopPixels)
            pending_click_ = false;
    }

    if (!dragging_ && (event.buttons & SDL_BUTTON_LMASK) == 0)
        return;
    yaw_ += event.delta.x * 0.008f;
    pitch_ += event.delta.y * 0.008f;
    clamp_camera();
    request_redraw();
}

void SatViewHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGui::GetIO().AddMouseWheelEvent(event.delta.x, event.delta.y);
        if (ImGui::GetIO().WantCaptureMouse)
        {
            request_redraw();
            return;
        }
    }

    distance_ *= std::pow(0.88f, event.delta.y);
    clamp_camera();
    request_redraw();
}

void SatViewHost::on_key(const KeyEvent& event)
{
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.mod & kModCtrl) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.mod & kModShift) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.mod & kModAlt) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (event.mod & kModSuper) != 0);
        const ImGuiKey key = sdl_scancode_to_imgui_key(event.scancode);
        if (key != ImGuiKey_None)
            io.AddKeyEvent(key, event.pressed);

        if (io.WantCaptureKeyboard)
        {
            request_redraw();
            return;
        }
    }

    if (!event.pressed)
        return;
    if (event.keycode == SDLK_F1 || event.scancode == SDL_SCANCODE_F1)
    {
        show_ui_panel_ = !show_ui_panel_;
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_ESCAPE && selected_norad_catalog_id_.has_value())
    {
        selected_norad_catalog_id_.reset();
        simulation_settings_dirty_ = true;
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_SPACE)
    {
        paused_ = !paused_;
        sync_simulation_controls();
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_LEFTBRACKET)
    {
        time_speed_ = std::max(1.0f, time_speed_ * 0.5f);
        sync_simulation_controls();
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_RIGHTBRACKET)
    {
        time_speed_ = std::min(3600.0f, time_speed_ * 2.0f);
        sync_simulation_controls();
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_HOME)
    {
        reset_camera();
        return;
    }
    if (event.keycode == SDLK_R)
    {
        catalog_service_.request_refresh();
        request_redraw();
        return;
    }
}

void SatViewHost::on_text_input(const TextInputEvent& event)
{
    if (!imgui_context_ || event.text.empty())
        return;

    ImGui::SetCurrentContext(imgui_context_);
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharactersUTF8(event.text.c_str());
    if (io.WantTextInput || io.WantCaptureKeyboard)
        request_redraw();
}

void SatViewHost::on_focus_lost()
{
    dragging_ = false;
    pending_click_ = false;
}

bool SatViewHost::dispatch_action(std::string_view action)
{
    if (action == "toggle_host_ui" || action == "toggle_ui_panels" || action == "satview_toggle_ui")
    {
        show_ui_panel_ = !show_ui_panel_;
        request_redraw();
        return true;
    }
    if (action == "satview_pause" || action == "satview_toggle_pause")
    {
        paused_ = !paused_;
        sync_simulation_controls();
        request_redraw();
        return true;
    }
    if (action == "satview_time_slower")
    {
        time_speed_ = std::max(1.0f, time_speed_ * 0.5f);
        sync_simulation_controls();
        request_redraw();
        return true;
    }
    if (action == "satview_time_faster")
    {
        time_speed_ = std::min(3600.0f, time_speed_ * 2.0f);
        sync_simulation_controls();
        request_redraw();
        return true;
    }
    if (action == "satview_reset_camera")
    {
        reset_camera();
        return true;
    }
    if (action == "satview_refresh_catalog")
    {
        catalog_service_.request_refresh();
        request_redraw();
        return true;
    }
    if (action == "satview_clear_selection")
    {
        selected_norad_catalog_id_.reset();
        simulation_settings_dirty_ = true;
        request_redraw();
        return true;
    }
    if (action == "quit" || action == "request_quit")
    {
        running_ = false;
        if (callbacks_)
            callbacks_->request_quit();
        return true;
    }
    return false;
}

void SatViewHost::request_close()
{
    catalog_service_.stop();
    if (simulation_worker_)
        simulation_worker_->stop();
    running_ = false;
}

std::string SatViewHost::status_text() const
{
    const std::string mode = paused_ ? "satview paused" : "satview earth";
    const std::string catalog_status = catalog_service_.status_text();
    auto snapshot = simulation_worker_ ? simulation_worker_->acquire_latest() : SatViewSnapshotExchange::ReadGuard{};
    const std::string propagation_status = snapshot ? snapshot->status_text : std::string{};
    if (catalog_status.empty() && propagation_status.empty())
        return mode;
    if (propagation_status.empty())
        return mode + " | " + catalog_status;
    if (catalog_status.empty())
        return mode + " | " + propagation_status;
    return mode + " | " + catalog_status + " | " + propagation_status;
}

Color SatViewHost::default_background() const
{
    return Color(0.005f, 0.008f, 0.018f, 1.0f);
}

HostRuntimeState SatViewHost::runtime_state() const
{
    HostRuntimeState state;
    state.content_ready = true;
    state.last_activity_time = last_activity_time_;
    return state;
}

HostDebugState SatViewHost::debug_state() const
{
    HostDebugState state;
    state.name = "SatView";
    state.grid_cols = 0;
    state.grid_rows = 0;
    state.dirty_cells = 0;
    return state;
}

void SatViewHost::attach_imgui_host(IImGuiHost& host)
{
    imgui_backend_ = &host;
    if (!imgui_context_)
        return;

    ImGui::SetCurrentContext(imgui_context_);
    host.initialize_imgui_backend();
    host.rebuild_imgui_font_texture();
}

void SatViewHost::set_imgui_font(const std::string& path, float size_pixels)
{
    imgui_font_path_ = path;
    imgui_font_size_pixels_ = size_pixels;
    if (!imgui_context_)
        return;

    ImGui::SetCurrentContext(imgui_context_);
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    if (!imgui_font_path_.empty() && imgui_font_size_pixels_ > 0.0f)
        io.Fonts->AddFontFromFileTTF(imgui_font_path_.c_str(), imgui_font_size_pixels_);
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();
    if (imgui_backend_)
        imgui_backend_->rebuild_imgui_font_texture();
}

void SatViewHost::request_redraw()
{
    last_activity_time_ = std::chrono::steady_clock::now();
    if (callbacks_)
        callbacks_->request_frame();
}

void SatViewHost::sync_simulation_controls()
{
    if (simulation_worker_)
        simulation_worker_->set_controls(time_speed_, paused_);
}

void SatViewHost::sync_simulation_render_settings()
{
    if (simulation_worker_)
    {
        simulation_worker_->set_render_settings(
            track_satellite_limit_,
            track_sample_count_,
            selected_norad_catalog_id_);
    }
}

void SatViewHost::clamp_camera()
{
    pitch_ = std::clamp(pitch_, -1.35f, 1.35f);
    distance_ = std::clamp(distance_, 1.7f, 12.0f);
}

void SatViewHost::reset_camera()
{
    const double simulation_seconds = simulation_worker_
        ? simulation_worker_->current_simulation_seconds()
        : last_draw_simulation_seconds_;
    const glm::vec3 sun = sun_direction(simulation_seconds);
    yaw_ = std::atan2(sun.x, sun.z) + 0.65f;
    pitch_ = 0.25f;
    distance_ = 3.6f;
    request_redraw();
}

void SatViewHost::render_host_imgui(float dt, const SatViewSimulationSnapshot* snapshot)
{
    if (!imgui_context_ || !imgui_backend_)
        return;

    ImGui::SetCurrentContext(imgui_context_);
    imgui_backend_->begin_imgui_frame();
    ImGuiIO& io = ImGui::GetIO();
    const int pixel_w = std::max(1, viewport_.pixel_size.x);
    const int pixel_h = std::max(1, viewport_.pixel_size.y);
    io.DisplaySize = ImVec2(
        static_cast<float>(viewport_.pixel_pos.x + pixel_w),
        static_cast<float>(viewport_.pixel_pos.y + pixel_h));
    io.DeltaTime = dt > 0.0f ? dt : (1.0f / 60.0f);
    ImGui::NewFrame();

    if (show_ui_panel_)
        render_control_panel(snapshot);

    ImGui::Render();
}

void SatViewHost::render_control_panel(const SatViewSimulationSnapshot* snapshot)
{
    const int panel_x = viewport_.pixel_pos.x + 12;
    const int panel_y = viewport_.pixel_pos.y + 12;
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(panel_x), static_cast<float>(panel_y)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(kControlPanelDefaultWidth, kControlPanelDefaultHeight), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(kControlPanelMinWidth, kControlPanelMinHeight),
        ImVec2(100000.0f, 100000.0f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("SatView", &show_ui_panel_, flags))
    {
        ImGui::End();
        return;
    }

    bool changed = false;
    auto set_control_width = [](const char* label) {
        ImGui::SetNextItemWidth(control_widget_width(label));
    };

    if (ImGui::Button(paused_ ? "Resume" : "Pause"))
    {
        paused_ = !paused_;
        sync_simulation_controls();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Camera"))
    {
        reset_camera();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
    {
        catalog_service_.request_refresh();
        changed = true;
    }

    float speed = time_speed_;
    set_control_width("Speed");
    if (ImGui::SliderFloat("Speed", &speed, 1.0f, 3600.0f, "%.0fx", ImGuiSliderFlags_Logarithmic))
    {
        time_speed_ = std::clamp(speed, 1.0f, 3600.0f);
        sync_simulation_controls();
        changed = true;
    }

    ImGui::SeparatorText("Visuals");
    int color_mode_index = color_mode_ == SatViewColorMode::ObjectType ? 1 : 0;
    const char* color_modes[] = { "Orbit Class", "Object Type" };
    set_control_width("Color");
    if (ImGui::Combo("Color", &color_mode_index, color_modes, 2))
    {
        color_mode_ = color_mode_index == 1 ? SatViewColorMode::ObjectType : SatViewColorMode::OrbitClass;
        changed = true;
    }

    int track_display_index = track_display_mode_ == SatViewTrackDisplayMode::SelectedOnly ? 1 : 0;
    const char* track_display_modes[] = { "All Sampled", "Selected Only" };
    set_control_width("Paths");
    if (ImGui::Combo("Paths", &track_display_index, track_display_modes, 2))
    {
        track_display_mode_ = track_display_index == 1
            ? SatViewTrackDisplayMode::SelectedOnly
            : SatViewTrackDisplayMode::AllSampled;
        changed = true;
    }

    int track_limit = static_cast<int>(track_satellite_limit_);
    set_control_width("Track count");
    if (ImGui::SliderInt("Track count", &track_limit, 32, 2048))
    {
        track_satellite_limit_ = static_cast<std::size_t>(std::max(32, track_limit));
        simulation_settings_dirty_ = true;
        changed = true;
    }

    int track_samples = static_cast<int>(track_sample_count_);
    set_control_width("Track samples");
    if (ImGui::SliderInt("Track samples", &track_samples, 12, 144))
    {
        track_sample_count_ = static_cast<std::size_t>(std::max(12, track_samples));
        simulation_settings_dirty_ = true;
        changed = true;
    }

    static constexpr std::size_t kMarkerLimitValues[] = { 0, 8192, 4096, 2048, 1024, 512 };
    static constexpr const char* kMarkerLimitLabels[] = { "All", "8192", "4096", "2048", "1024", "512" };
    int marker_limit_index = 0;
    for (int i = 0; i < 6; ++i)
    {
        if (marker_satellite_limit_ == kMarkerLimitValues[i])
        {
            marker_limit_index = i;
            break;
        }
    }
    set_control_width("Marker cap");
    if (ImGui::Combo("Marker cap", &marker_limit_index, kMarkerLimitLabels, 6))
    {
        marker_satellite_limit_ = kMarkerLimitValues[marker_limit_index];
        changed = true;
    }

    ImGui::SeparatorText("Filters");
    set_control_width("Search");
    if (ImGui::InputText("Search", search_buffer_, sizeof(search_buffer_)))
    {
        filter_.search_text = search_buffer_;
        changed = true;
    }
    set_control_width("Type");
    if (ImGui::InputText("Type", object_type_buffer_, sizeof(object_type_buffer_)))
    {
        filter_.object_type_text = object_type_buffer_;
        changed = true;
    }
    set_control_width("Source");
    if (ImGui::InputText("Source", source_buffer_, sizeof(source_buffer_)))
    {
        filter_.source_text = source_buffer_;
        changed = true;
    }

    float max_age_days = static_cast<float>(filter_.max_epoch_age_days);
    set_control_width("Age days");
    if (ImGui::DragFloat("Age days", &max_age_days, 0.1f, 0.0f, 30.0f, "%.1f"))
    {
        filter_.max_epoch_age_days = static_cast<double>(std::max(0.0f, max_age_days));
        changed = true;
    }

    changed |= ImGui::Checkbox("LEO", &filter_.show_low_earth);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("MEO", &filter_.show_medium_earth);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("GEO", &filter_.show_geosynchronous);
    changed |= ImGui::Checkbox("HEO", &filter_.show_highly_elliptical);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Other", &filter_.show_other);

    ImGui::SeparatorText("Catalog");
    const std::string catalog_status = catalog_service_.status_text();
    ImGui::TextWrapped("%s", catalog_status.empty() ? "catalog pending" : catalog_status.c_str());
    const std::string_view source_label = snapshot ? std::string_view(snapshot->source_label) : std::string_view{};
    if (source_label.empty())
        ImGui::TextDisabled("Source: pending");
    else
        ImGui::Text("Source: %.*s", static_cast<int>(source_label.size()), source_label.data());
    const std::size_t filtered_markers = visible_state_count(snapshot);
    const std::size_t rendered_markers = marker_satellite_limit_ == 0
        ? filtered_markers
        : std::min(filtered_markers, marker_satellite_limit_);
    const std::size_t total_markers = snapshot ? snapshot->states.size() : 0;
    const std::size_t total_tracks = (snapshot && snapshot->tracks) ? snapshot->tracks->size() : 0;
    ImGui::Text("Markers: %zu / %zu", rendered_markers, total_markers);
    ImGui::Text("Paths: %zu / %zu", visible_track_count(snapshot), total_tracks);

    ImGui::SeparatorText("Selection");
    if (const SatellitePropagatedState* selected = selected_satellite(snapshot))
    {
        ImGui::TextWrapped("%s", selected->object_name.c_str());
        ImGui::Text("NORAD: %lld", static_cast<long long>(selected->norad_catalog_id));
        if (!selected->object_id.empty())
            ImGui::Text("Object ID: %s", selected->object_id.c_str());
        ImGui::Text("Orbit: %.*s",
            static_cast<int>(orbit_class_name(selected->orbit_class).size()),
            orbit_class_name(selected->orbit_class).data());
        if (!selected->object_type.empty())
            ImGui::Text("Type: %s", selected->object_type.c_str());
        ImGui::Text("Kind: %.*s",
            static_cast<int>(satellite_object_kind_name(selected->object_kind).size()),
            satellite_object_kind_name(selected->object_kind).data());
        if (!selected->classification_type.empty())
            ImGui::Text("Class: %s", selected->classification_type.c_str());
        ImGui::Text("Period: %.1f min", selected->period_minutes);
        ImGui::Text("Epoch age: %.1f h", selected->minutes_since_epoch / 60.0);
        const double altitude_km = glm::length(selected->teme_position_km) - kSatViewEarthEquatorialRadiusKm;
        ImGui::Text("Altitude: %.0f km", altitude_km);
        const double speed_km_s = glm::length(selected->teme_velocity_km_per_s);
        ImGui::Text("Speed: %.2f km/s", speed_km_s);
        if (ImGui::Button("Clear Selection"))
        {
            selected_norad_catalog_id_.reset();
            simulation_settings_dirty_ = true;
            changed = true;
        }
    }
    else
    {
        ImGui::TextDisabled("No satellite selected.");
    }

    if (changed)
    {
        clear_selection_if_hidden(snapshot);
        request_redraw();
    }

    ImGui::End();
}

std::size_t SatViewHost::visible_state_count(const SatViewSimulationSnapshot* snapshot) const
{
    if (!snapshot)
        return 0;

    const std::string_view source_label = snapshot->source_label;
    std::size_t count = 0;
    for (const SatellitePropagatedState& state : snapshot->states)
    {
        if (satellite_visible(filter_, state, source_label))
            ++count;
    }
    return count;
}

std::size_t SatViewHost::visible_track_count(const SatViewSimulationSnapshot* snapshot) const
{
    if (!snapshot || !snapshot->tracks)
        return 0;

    const std::string_view source_label = snapshot->source_label;
    std::size_t count = 0;
    for (const SatelliteOrbitTrack& track : *snapshot->tracks)
    {
        if (track_display_mode_ == SatViewTrackDisplayMode::SelectedOnly
            && (!selected_norad_catalog_id_.has_value()
                || track.norad_catalog_id != *selected_norad_catalog_id_))
        {
            continue;
        }

        if (satellite_visible(filter_, track, source_label))
            ++count;
    }
    return count;
}

const SatellitePropagatedState* SatViewHost::selected_satellite(const SatViewSimulationSnapshot* snapshot) const
{
    if (!snapshot || !selected_norad_catalog_id_.has_value())
        return nullptr;

    const std::string_view source_label = snapshot->source_label;
    for (const SatellitePropagatedState& state : snapshot->states)
    {
        if (state.norad_catalog_id == *selected_norad_catalog_id_
            && satellite_visible(filter_, state, source_label))
        {
            return &state;
        }
    }
    return nullptr;
}

void SatViewHost::clear_selection_if_hidden(const SatViewSimulationSnapshot* snapshot)
{
    if (selected_norad_catalog_id_.has_value() && snapshot && !selected_satellite(snapshot))
    {
        selected_norad_catalog_id_.reset();
        simulation_settings_dirty_ = true;
    }
}

void SatViewHost::select_nearest_satellite(const glm::ivec2& screen_pos)
{
    auto snapshot_guard = simulation_worker_ ? simulation_worker_->acquire_latest() : SatViewSnapshotExchange::ReadGuard{};
    const SatViewSimulationSnapshot* snapshot = snapshot_guard.get();
    if (!snapshot || snapshot->states.empty())
        return;

    const int pixel_w = std::max(1, viewport_.pixel_size.x);
    const int pixel_h = std::max(1, viewport_.pixel_size.y);
    const float aspect = static_cast<float>(pixel_w) / static_cast<float>(pixel_h);
    const glm::vec3 eye = camera_position(yaw_, pitch_, distance_);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(42.0f), aspect, 0.05f, 64.0f);
    const glm::mat4 view_proj = proj * view;
    const std::string_view source_label = snapshot->source_label;
    const double render_seconds = render_simulation_seconds(*snapshot);

    std::optional<std::int64_t> nearest_id;
    float nearest_distance_sq = static_cast<float>(
        kClickSelectionMaxDistancePixels * kClickSelectionMaxDistancePixels);
    std::size_t visible_marker_index = 0;
    for (std::size_t state_index = 0; state_index < snapshot->states.size(); ++state_index)
    {
        const SatellitePropagatedState& state = snapshot->states[state_index];
        if (!satellite_visible(filter_, state, source_label))
            continue;

        if (marker_satellite_limit_ != 0 && visible_marker_index >= marker_satellite_limit_)
            break;
        ++visible_marker_index;

        const glm::vec3 world = to_vec3(
            interpolated_teme_position(*snapshot, state_index, render_seconds) / kSatViewEarthEquatorialRadiusKm);
        const glm::vec4 clip = view_proj * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0f)
            continue;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f || ndc.z < 0.0f || ndc.z > 1.0f)
            continue;

        const float sx = static_cast<float>(viewport_.pixel_pos.x)
            + (ndc.x * 0.5f + 0.5f) * static_cast<float>(pixel_w);
        const float sy = static_cast<float>(viewport_.pixel_pos.y)
            + (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(pixel_h);
        const glm::vec2 delta = glm::vec2(sx, sy) - glm::vec2(screen_pos);
        const float distance_sq = glm::dot(delta, delta);
        if (distance_sq < nearest_distance_sq)
        {
            nearest_distance_sq = distance_sq;
            nearest_id = state.norad_catalog_id;
        }
    }

    if (nearest_id != selected_norad_catalog_id_)
    {
        selected_norad_catalog_id_ = nearest_id;
        sync_simulation_render_settings();
    }
    request_redraw();
}

std::unique_ptr<IHost> create_satview_host()
{
    return std::make_unique<SatViewHost>();
}

void register_satview_host_provider(HostProviderRegistry& registry)
{
    registry.register_provider(HostKind::SatView, create_satview_host);
}

} // namespace draxul::satview
