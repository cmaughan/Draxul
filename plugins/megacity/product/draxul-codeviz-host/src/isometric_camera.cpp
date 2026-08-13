#include <draxul/isometric_camera.h>

#include <draxul/perf_timing.h>

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace draxul
{

namespace
{

constexpr float kDefaultPitchAngle = 0.72425002f; // about 41.5 degrees
constexpr float kMinPitchAngle = 0.08726646f; // 5 degrees
constexpr float kMaxPitchAngle = 1.22173048f; // 70 degrees
constexpr float kMinZoomWorldSpan = 5.0f;
constexpr float kPerspectiveFovY = 0.87266463f; // 50 degrees

float orbit_radius_for_half_height(float half_height, float pitch_angle)
{
    const float target_distance = std::max(half_height, 1e-3f) / std::tan(kPerspectiveFovY * 0.5f);
    return std::max(1e-3f, target_distance * std::cos(pitch_angle));
}

float far_plane_for_view(const glm::vec3& follow_offset, float visible_half_height)
{
    return std::max(100.0f, glm::length(follow_offset) + visible_half_height * 4.0f + 16.0f);
}

glm::vec2 normalized_planar(const glm::vec3& v, const glm::vec2& fallback)
{
    const glm::vec2 planar{ v.x, v.z };
    const float len = glm::length(planar);
    if (len <= 1e-5f)
        return fallback;
    return planar / len;
}

} // namespace

void IsometricCamera::set_viewport(int pixel_w, int pixel_h)
{
    viewport_pixel_h_ = std::max(pixel_h, 1);
    aspect_ = (pixel_h > 0) ? static_cast<float>(pixel_w) / static_cast<float>(pixel_h) : 1.0f;
}

void IsometricCamera::look_at_world_center(float world_w, float world_h)
{
    frame_world_bounds(0.0f, world_w, 0.0f, world_h);
}

void IsometricCamera::frame_world_bounds(float min_x, float max_x, float min_z, float max_z)
{
    PERF_MEASURE();
    if (min_x > max_x || min_z > max_z)
    {
        const float max_dim = kMinZoomWorldSpan;
        min_ortho_half_height_ = kMinZoomWorldSpan * 0.5f;
        max_ortho_half_height_ = std::max(min_ortho_half_height_, max_dim);
        ortho_half_height_ = std::clamp(max_dim * 0.8f, min_ortho_half_height_, max_ortho_half_height_);
        orbit_radius_ = std::sqrt(2.0f) * max_dim;
        yaw_angle_ = -2.35619449f;
        pitch_angle_ = kDefaultPitchAngle;
        update_zoom_bounds();
        update_follow_offset();
        set_target({ 0.0f, 0.0f, 0.0f });
        return;
    }

    const float world_w = max_x - min_x;
    const float world_h = max_z - min_z;
    const float max_dim = std::max({ world_w, world_h, kMinZoomWorldSpan });
    min_ortho_half_height_ = kMinZoomWorldSpan * 0.5f;
    max_ortho_half_height_ = std::max(min_ortho_half_height_, max_dim);
    ortho_half_height_ = std::clamp(max_dim * 0.8f, min_ortho_half_height_, max_ortho_half_height_);
    orbit_radius_ = std::sqrt(2.0f) * max_dim;
    yaw_angle_ = -2.35619449f;
    pitch_angle_ = kDefaultPitchAngle;
    update_zoom_bounds();
    update_follow_offset();
    set_target({ (min_x + max_x) * 0.5f, 0.0f, (min_z + max_z) * 0.5f });
}

void IsometricCamera::reframe_world_bounds(float min_x, float max_x, float min_z, float max_z)
{
    if (min_x > max_x || min_z > max_z)
    {
        const float max_dim = kMinZoomWorldSpan;
        min_ortho_half_height_ = kMinZoomWorldSpan * 0.5f;
        max_ortho_half_height_ = std::max(min_ortho_half_height_, max_dim);
        ortho_half_height_ = std::clamp(ortho_half_height_, min_ortho_half_height_, max_ortho_half_height_);
        orbit_radius_ = std::sqrt(2.0f) * max_dim;
        update_zoom_bounds();
        orbit_radius_ = std::clamp(orbit_radius_, min_orbit_radius_, max_orbit_radius_);
        update_follow_offset();
        position_ = target_ + follow_offset_;
        return;
    }

    const float world_w = max_x - min_x;
    const float world_h = max_z - min_z;
    const float max_dim = std::max({ world_w, world_h, kMinZoomWorldSpan });
    min_ortho_half_height_ = kMinZoomWorldSpan * 0.5f;
    max_ortho_half_height_ = std::max(min_ortho_half_height_, max_dim);
    ortho_half_height_ = std::clamp(ortho_half_height_, min_ortho_half_height_, max_ortho_half_height_);
    orbit_radius_ = std::sqrt(2.0f) * max_dim;
    update_zoom_bounds();
    orbit_radius_ = std::clamp(orbit_radius_, min_orbit_radius_, max_orbit_radius_);
    update_follow_offset();
    position_ = target_ + follow_offset_;
}

void IsometricCamera::set_target(const glm::vec3& target)
{
    target_ = target;
    position_ = target_ + follow_offset_;
}

void IsometricCamera::translate_target(float dx, float dz)
{
    set_target(target_ + glm::vec3(dx, 0.0f, dz));
}

void IsometricCamera::orbit_target(float radians)
{
    if (orbit_radius_ <= 0.0f)
        return;
    yaw_angle_ += radians;
    update_follow_offset();
    position_ = target_ + follow_offset_;
}

void IsometricCamera::zoom_by(float log_delta)
{
    if (log_delta == 0.0f)
        return;

    if (projection_mode_ == CodeVizProjectionMode::Perspective)
    {
        orbit_radius_ = std::clamp(orbit_radius_ * std::exp(log_delta), min_orbit_radius_, max_orbit_radius_);
        update_follow_offset();
        position_ = target_ + follow_offset_;
        return;
    }

    ortho_half_height_
        = std::clamp(ortho_half_height_ * std::exp(log_delta), min_ortho_half_height_, max_ortho_half_height_);
    update_far_plane();
}

void IsometricCamera::adjust_pitch(float radians)
{
    if (radians == 0.0f)
        return;

    pitch_angle_ = std::clamp(pitch_angle_ + radians, kMinPitchAngle, kMaxPitchAngle);
    update_zoom_bounds();
    orbit_radius_ = std::clamp(orbit_radius_, min_orbit_radius_, max_orbit_radius_);
    update_follow_offset();
    position_ = target_ + follow_offset_;
}

void IsometricCamera::set_projection_mode(CodeVizProjectionMode mode)
{
    if (projection_mode_ == mode)
        return;

    projection_mode_ = mode;
    update_zoom_bounds();
    orbit_radius_ = std::clamp(orbit_radius_, min_orbit_radius_, max_orbit_radius_);
    update_follow_offset();
    position_ = target_ + follow_offset_;
}

glm::vec2 IsometricCamera::pan_delta_for_screen_drag(const glm::vec2& pixel_delta) const
{
    const glm::vec3 forward = glm::normalize(target_ - position_);
    const glm::vec3 right_3d = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up_3d = glm::normalize(glm::cross(right_3d, forward));
    const glm::vec2 right = normalized_planar(right_3d, glm::vec2(1.0f, 0.0f));
    const glm::vec2 up = normalized_planar(up_3d, glm::vec2(0.0f, 1.0f));

    const float world_units_per_pixel = (2.0f * current_projection_half_height()) / static_cast<float>(viewport_pixel_h_);
    const float up_planar_length = std::max(glm::length(glm::vec2(up_3d.x, up_3d.z)), 1e-5f);

    return (-pixel_delta.x * world_units_per_pixel) * right
        + ((pixel_delta.y * world_units_per_pixel) / up_planar_length) * up;
}

glm::vec2 IsometricCamera::planar_right_vector() const
{
    const glm::vec3 forward = glm::normalize(target_ - position_);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    return normalized_planar(right, glm::vec2(1.0f, 0.0f));
}

glm::vec2 IsometricCamera::planar_up_vector() const
{
    const glm::vec3 forward = glm::normalize(target_ - position_);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    return normalized_planar(up, glm::vec2(0.0f, 1.0f));
}

glm::mat4 IsometricCamera::view_matrix() const
{
    return glm::lookAtRH(position_, target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 IsometricCamera::proj_matrix() const
{
    if (projection_mode_ == CodeVizProjectionMode::Perspective)
        return glm::perspectiveRH_ZO(kPerspectiveFovY, aspect_, 0.1f, far_plane_);

    const float half_width = ortho_half_height_ * aspect_;
    return glm::orthoRH_ZO(
        -half_width, half_width, -ortho_half_height_, ortho_half_height_, 0.1f, far_plane_);
}

IsometricCameraState IsometricCamera::state() const
{
    return IsometricCameraState{
        .target = target_,
        .yaw = yaw_angle_,
        .pitch = pitch_angle_,
        .orbit_radius = orbit_radius_,
        .zoom_half_height = ortho_half_height_,
        .projection_mode = projection_mode_,
    };
}

void IsometricCamera::apply_state(const IsometricCameraState& state)
{
    projection_mode_ = state.projection_mode;
    yaw_angle_ = state.yaw;
    pitch_angle_ = std::clamp(state.pitch, kMinPitchAngle, kMaxPitchAngle);
    ortho_half_height_ = std::clamp(state.zoom_half_height, min_ortho_half_height_, max_ortho_half_height_);
    update_zoom_bounds();
    orbit_radius_ = std::clamp(state.orbit_radius, min_orbit_radius_, max_orbit_radius_);
    update_follow_offset();
    set_target(state.target);
}

GroundFootprint IsometricCamera::visible_ground_footprint(float plane_y) const
{
    PERF_MEASURE();
    const glm::mat4 inv_view_proj = glm::inverse(proj_matrix() * view_matrix());

    GroundFootprint footprint;
    footprint.min_x = std::numeric_limits<float>::max();
    footprint.max_x = std::numeric_limits<float>::lowest();
    footprint.min_z = std::numeric_limits<float>::max();
    footprint.max_z = std::numeric_limits<float>::lowest();

    const glm::vec2 corners[4] = {
        { -1.0f, -1.0f },
        { 1.0f, -1.0f },
        { 1.0f, 1.0f },
        { -1.0f, 1.0f },
    };

    for (const glm::vec2& corner : corners)
    {
        glm::vec4 near_world = inv_view_proj * glm::vec4(corner, 0.0f, 1.0f);
        glm::vec4 far_world = inv_view_proj * glm::vec4(corner, 1.0f, 1.0f);
        near_world /= near_world.w;
        far_world /= far_world.w;

        const glm::vec3 near_pos = glm::vec3(near_world);
        const glm::vec3 far_pos = glm::vec3(far_world);
        const glm::vec3 ray = far_pos - near_pos;
        if (std::abs(ray.y) < 1e-5f)
            continue;

        const float t = (plane_y - near_pos.y) / ray.y;
        const glm::vec3 point = near_pos + ray * t;
        footprint.min_x = std::min(footprint.min_x, point.x);
        footprint.max_x = std::max(footprint.max_x, point.x);
        footprint.min_z = std::min(footprint.min_z, point.z);
        footprint.max_z = std::max(footprint.max_z, point.z);
    }

    if (footprint.min_x > footprint.max_x || footprint.min_z > footprint.max_z)
    {
        const float half_height = current_projection_half_height();
        footprint.min_x = target_.x - half_height;
        footprint.max_x = target_.x + half_height;
        footprint.min_z = target_.z - half_height;
        footprint.max_z = target_.z + half_height;
    }

    return footprint;
}

float IsometricCamera::current_projection_half_height() const
{
    if (projection_mode_ == CodeVizProjectionMode::Perspective)
        return std::max(1e-3f, glm::length(follow_offset_) * std::tan(kPerspectiveFovY * 0.5f));
    return ortho_half_height_;
}

void IsometricCamera::update_zoom_bounds()
{
    min_orbit_radius_ = orbit_radius_for_half_height(min_ortho_half_height_, pitch_angle_);
    max_orbit_radius_
        = std::max(min_orbit_radius_, orbit_radius_for_half_height(max_ortho_half_height_, pitch_angle_));
}

void IsometricCamera::update_follow_offset()
{
    const float planar_radius = std::max(orbit_radius_, 1e-3f);
    const float height = std::tan(pitch_angle_) * planar_radius;
    follow_offset_ = glm::vec3(
        std::cos(yaw_angle_) * planar_radius,
        height,
        std::sin(yaw_angle_) * planar_radius);
    update_far_plane();
}

void IsometricCamera::update_far_plane()
{
    far_plane_ = far_plane_for_view(follow_offset_, current_projection_half_height());
}

} // namespace draxul
