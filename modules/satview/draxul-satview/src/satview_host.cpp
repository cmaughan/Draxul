#include <draxul/satview/satview_host.h>

#include "satview_scene_pass.h"

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cmath>
#include <draxul/host_registry.h>
#include <draxul/log.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <utility>
#include <vector>

namespace draxul::satview
{

namespace
{

constexpr auto kFrameTick = std::chrono::milliseconds(33);
constexpr auto kPropagationTick = std::chrono::seconds(1);
constexpr std::size_t kPreviewTrackSatelliteLimit = 256;
constexpr std::size_t kPreviewTrackSampleCount = 48;

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

void append_track_vertices(std::vector<SatViewSceneVertex>& vertices, const SatellitePropagationResult& snapshot)
{
    for (const SatelliteOrbitTrack& track : snapshot.tracks)
    {
        const auto& points = track.render_teme_points_earth_radii;
        if (points.size() < 2)
            continue;

        const glm::vec4 color = orbit_class_color(track.orbit_class, 0.62f);
        for (std::size_t i = 1; i < points.size(); ++i)
            append_line(vertices, to_vec3(points[i - 1]), to_vec3(points[i]), color);
        append_line(vertices, to_vec3(points.back()), to_vec3(points.front()), color * glm::vec4(1.0f, 1.0f, 1.0f, 0.82f));
    }
}

void append_marker_vertices(
    std::vector<SatViewSceneVertex>& vertices,
    const SatellitePropagationResult& snapshot,
    glm::vec3 camera_right,
    glm::vec3 camera_up)
{
    for (const SatellitePropagatedState& state : snapshot.states)
    {
        const glm::vec3 center = to_vec3(state.teme_position_km / kSatViewEarthEquatorialRadiusKm);
        const float range = glm::length(center);
        const float size = std::clamp(0.006f + range * 0.0022f, 0.008f, 0.026f);
        const glm::vec4 color = glm::mix(
            orbit_class_color(state.orbit_class, 0.95f),
            glm::vec4(1.0f),
            0.18f);

        append_line(vertices, center - camera_right * size, center + camera_right * size, color);
        append_line(vertices, center - camera_up * size, center + camera_up * size, color);
    }
}

float earth_rotation(double seconds)
{
    const double day_fraction = std::fmod(seconds / 86164.0905, 1.0);
    return static_cast<float>(day_fraction * glm::two_pi<double>());
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
    catalog_service_.start();
    simulated_seconds_ = unix_seconds_now();
    const glm::vec3 sun = sun_direction(simulated_seconds_);
    yaw_ = std::atan2(sun.x, sun.z) + 0.65f;
    pitch_ = 0.25f;
    last_pump_time_ = std::chrono::steady_clock::now();
    last_activity_time_ = last_pump_time_;
    next_propagation_time_ = last_pump_time_;
    last_propagation_simulated_seconds_ = simulated_seconds_;
    scene_pass_ = std::make_shared<SatViewScenePass>();
    running_ = true;
    callbacks.set_window_title("SatView");
    request_redraw();
    return true;
}

void SatViewHost::shutdown()
{
    catalog_service_.stop();
    running_ = false;
    dragging_ = false;
    scene_pass_.reset();
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
    const bool propagation_model_changed = rebuild_propagation_model_if_needed();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last_pump_time_).count();
    last_pump_time_ = now;
    if (!paused_)
        simulated_seconds_ += static_cast<double>(dt) * static_cast<double>(time_speed_);
    update_propagation_if_needed(propagation_model_changed);
    request_redraw();
}

void SatViewHost::draw(IFrameContext& frame)
{
    if (!scene_pass_)
        return;

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
    const glm::vec3 sun = sun_direction(simulated_seconds_);
    uniforms.sun_dir_time = glm::vec4(sun, static_cast<float>(std::fmod(simulated_seconds_, 86400.0)));
    uniforms.render_params = glm::vec4(
        static_cast<float>(kSatViewSphereLatitudeBands),
        static_cast<float>(kSatViewSphereLongitudeBands),
        earth_rotation(simulated_seconds_),
        paused_ ? 1.0f : 0.0f);
    scene_pass_->set_frame(uniforms);

    std::vector<SatViewSceneVertex> scene_vertices;
    scene_vertices.reserve(
        propagation_snapshot_.tracks.size() * kPreviewTrackSampleCount * 2
        + propagation_snapshot_.states.size() * 4);
    append_track_vertices(scene_vertices, propagation_snapshot_);
    append_marker_vertices(scene_vertices, propagation_snapshot_, camera_right, camera_up);
    scene_pass_->set_scene_vertices(scene_vertices);

    RenderViewport viewport;
    viewport.x = viewport_.pixel_pos.x;
    viewport.y = viewport_.pixel_pos.y;
    viewport.width = pixel_w;
    viewport.height = pixel_h;
    frame.record_render_pass(*scene_pass_, viewport);
    frame.flush_submit_chunk();
}

std::optional<std::chrono::steady_clock::time_point> SatViewHost::next_deadline() const
{
    if (!running_)
        return std::nullopt;
    return std::chrono::steady_clock::now() + kFrameTick;
}

void SatViewHost::on_mouse_button(const MouseButtonEvent& event)
{
    if (event.button != SDL_BUTTON_LEFT)
        return;
    dragging_ = event.pressed;
    last_activity_time_ = std::chrono::steady_clock::now();
}

void SatViewHost::on_mouse_move(const MouseMoveEvent& event)
{
    if (!dragging_ && (event.buttons & SDL_BUTTON_LMASK) == 0)
        return;
    yaw_ += event.delta.x * 0.008f;
    pitch_ += event.delta.y * 0.008f;
    clamp_camera();
    request_redraw();
}

void SatViewHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    distance_ *= std::pow(0.88f, event.delta.y);
    clamp_camera();
    request_redraw();
}

void SatViewHost::on_key(const KeyEvent& event)
{
    if (!event.pressed)
        return;
    if (event.keycode == SDLK_SPACE)
    {
        paused_ = !paused_;
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_LEFTBRACKET)
    {
        time_speed_ = std::max(1.0f, time_speed_ * 0.5f);
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_RIGHTBRACKET)
    {
        time_speed_ = std::min(3600.0f, time_speed_ * 2.0f);
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_R)
    {
        catalog_service_.request_refresh();
        request_redraw();
        return;
    }
}

bool SatViewHost::dispatch_action(std::string_view action)
{
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
    running_ = false;
}

std::string SatViewHost::status_text() const
{
    const std::string mode = paused_ ? "satview paused" : "satview earth";
    const std::string catalog_status = catalog_service_.status_text();
    if (catalog_status.empty() && propagation_status_.empty())
        return mode;
    if (propagation_status_.empty())
        return mode + " | " + catalog_status;
    if (catalog_status.empty())
        return mode + " | " + propagation_status_;
    return mode + " | " + catalog_status + " | " + propagation_status_;
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

void SatViewHost::request_redraw()
{
    last_activity_time_ = std::chrono::steady_clock::now();
    if (callbacks_)
        callbacks_->request_frame();
}

void SatViewHost::clamp_camera()
{
    pitch_ = std::clamp(pitch_, -1.35f, 1.35f);
    distance_ = std::clamp(distance_, 1.7f, 12.0f);
}

bool SatViewHost::rebuild_propagation_model_if_needed()
{
    const std::uint64_t generation = catalog_service_.catalog_generation();
    if (generation == propagation_catalog_generation_)
        return false;

    propagation_catalog_generation_ = generation;
    propagation_snapshot_ = {};
    propagation_status_.clear();
    if (generation == 0)
    {
        propagation_model_ = {};
        return true;
    }

    auto build = build_satellite_propagation_model(catalog_service_.catalog());
    if (!build)
    {
        propagation_model_ = {};
        propagation_status_ = "sgp4 unavailable";
        return true;
    }

    propagation_model_ = std::move(build.model);
    propagation_status_ = "sgp4 model " + std::to_string(propagation_model_.size()) + " sats";
    next_propagation_time_ = std::chrono::steady_clock::now();
    return true;
}

void SatViewHost::update_propagation_if_needed(bool force)
{
    if (propagation_model_.empty())
        return;

    const auto now = std::chrono::steady_clock::now();
    const double simulated_delta = std::abs(simulated_seconds_ - last_propagation_simulated_seconds_);
    if (!force && now < next_propagation_time_ && simulated_delta < 60.0)
        return;

    SatellitePropagationSettings settings;
    settings.track_satellite_limit = kPreviewTrackSatelliteLimit;
    settings.track_sample_count = kPreviewTrackSampleCount;

    propagation_snapshot_ = propagate_satellites(propagation_model_, simulated_seconds_, settings);
    if (!propagation_snapshot_)
    {
        propagation_status_ = "sgp4 " + propagation_snapshot_.error;
    }
    else
    {
        propagation_status_ = "sgp4 " + std::to_string(propagation_snapshot_.states.size()) + " positions";
        if (!propagation_snapshot_.tracks.empty())
            propagation_status_ += ", " + std::to_string(propagation_snapshot_.tracks.size()) + " tracks";
        if (propagation_snapshot_.failed_propagations != 0)
            propagation_status_ += ", " + std::to_string(propagation_snapshot_.failed_propagations) + " failed";
    }

    last_propagation_simulated_seconds_ = simulated_seconds_;
    next_propagation_time_ = now + kPropagationTick;
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
