#include "satview_ground_view.h"

#include <algorithm>
#include <cmath>
#include <draxul/satview/satview_propagation.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <numbers>

namespace draxul::satview
{

namespace
{

constexpr double kMaximumGroundTrackSegmentAngle = 4.0 * std::numbers::pi_v<double> / 180.0;
constexpr std::size_t kMaximumGroundTrackSubdivisions = 32;
constexpr double kVectorEpsilon = 1.0e-12;

glm::dvec3 ecef_surface_from_geodetic(SatViewGroundLocation location)
{
    const double cos_latitude = std::cos(location.latitude_radians);
    return glm::dvec3(
        cos_latitude * std::cos(location.longitude_radians),
        cos_latitude * std::sin(location.longitude_radians),
        std::sin(location.latitude_radians));
}

glm::dvec3 render_from_teme_earth_radii(const glm::dvec3& teme_earth_radii)
{
    return glm::dvec3(-teme_earth_radii.y, teme_earth_radii.z, -teme_earth_radii.x);
}

glm::dvec3 render_from_ecef_earth_radii(const glm::dvec3& ecef_earth_radii, double unix_seconds)
{
    const double sidereal_angle = greenwich_sidereal_angle_radians(unix_seconds);
    const double c = std::cos(sidereal_angle);
    const double s = std::sin(sidereal_angle);
    const glm::dvec3 teme(
        c * ecef_earth_radii.x - s * ecef_earth_radii.y,
        s * ecef_earth_radii.x + c * ecef_earth_radii.y,
        ecef_earth_radii.z);
    return render_from_teme_earth_radii(teme);
}

glm::dvec3 ecef_from_render_earth_radii(const glm::dvec3& render_position, double unix_seconds)
{
    const glm::dvec3 teme(-render_position.z, -render_position.x, render_position.y);
    const double sidereal_angle = greenwich_sidereal_angle_radians(unix_seconds);
    const double c = std::cos(sidereal_angle);
    const double s = std::sin(sidereal_angle);
    return glm::dvec3(
        c * teme.x + s * teme.y,
        -s * teme.x + c * teme.y,
        teme.z);
}

} // namespace

SatViewGroundLocation satview_ground_location_from_map_ndc(
    glm::vec2 ndc_position,
    glm::vec2 center_radians)
{
    constexpr double kPi = std::numbers::pi_v<double>;
    const double local_longitude = static_cast<double>(ndc_position.x) * kPi;
    const double local_latitude = static_cast<double>(ndc_position.y) * 0.5 * kPi;
    const double center_longitude = static_cast<double>(center_radians.x);
    const double center_latitude = static_cast<double>(center_radians.y);

    const double cos_center_lon = std::cos(center_longitude);
    const double sin_center_lon = std::sin(center_longitude);
    const double cos_center_lat = std::cos(center_latitude);
    const double sin_center_lat = std::sin(center_latitude);
    const glm::dvec3 center_axis(
        cos_center_lat * cos_center_lon,
        cos_center_lat * sin_center_lon,
        sin_center_lat);
    const glm::dvec3 east_axis(-sin_center_lon, cos_center_lon, 0.0);
    const glm::dvec3 north_axis(
        -sin_center_lat * cos_center_lon,
        -sin_center_lat * sin_center_lon,
        cos_center_lat);

    const double cos_local_lat = std::cos(local_latitude);
    const glm::dvec3 ecef = glm::normalize(
        center_axis * (cos_local_lat * std::cos(local_longitude))
        + east_axis * (cos_local_lat * std::sin(local_longitude))
        + north_axis * std::sin(local_latitude));
    return {
        std::atan2(ecef.y, ecef.x),
        std::asin(std::clamp(ecef.z, -1.0, 1.0)),
    };
}

glm::dvec3 satview_ground_render_position(
    SatViewGroundLocation location,
    double unix_seconds)
{
    return render_from_ecef_earth_radii(ecef_surface_from_geodetic(location), unix_seconds);
}

SatViewGroundLocation satview_ground_location_from_render_position(
    const glm::dvec3& render_position,
    double unix_seconds)
{
    const glm::dvec3 ecef = glm::normalize(
        ecef_from_render_earth_radii(render_position, unix_seconds));
    return {
        std::atan2(ecef.y, ecef.x),
        std::asin(std::clamp(ecef.z, -1.0, 1.0)),
    };
}

glm::quat satview_default_ground_camera_orientation()
{
    return glm::angleAxis(std::numbers::pi_v<float>, glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::quat satview_rotate_ground_camera(
    glm::quat local_camera_orientation,
    glm::vec2 yaw_pitch_delta_radians)
{
    const glm::quat yaw = glm::angleAxis(
        yaw_pitch_delta_radians.x,
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat pitch = glm::angleAxis(
        yaw_pitch_delta_radians.y,
        glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::normalize(local_camera_orientation * yaw * pitch);
}

glm::quat satview_ground_camera_world_orientation(
    const glm::dvec3& observer_render_position,
    glm::quat local_camera_orientation)
{
    const glm::vec3 eye = glm::vec3(observer_render_position);
    const glm::vec3 up = glm::normalize(eye);
    const glm::vec3 reference = std::abs(glm::dot(up, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.96f
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 east = glm::normalize(glm::cross(reference, up));
    const glm::vec3 north = glm::normalize(glm::cross(up, east));
    const glm::quat local_to_world = glm::quat_cast(glm::mat3(east, north, up));
    return glm::normalize(local_to_world * glm::normalize(local_camera_orientation));
}

glm::mat4 satview_ground_view_matrix(
    const glm::dvec3& observer_render_position,
    glm::quat local_camera_orientation)
{
    const glm::vec3 eye = glm::vec3(observer_render_position);
    const glm::quat world_orientation = satview_ground_camera_world_orientation(
        observer_render_position,
        local_camera_orientation);
    return glm::mat4_cast(glm::conjugate(world_orientation))
        * glm::translate(glm::mat4(1.0f), -eye);
}

double satview_ground_visibility_dot(
    const glm::dvec3& satellite_render_position,
    const glm::dvec3& observer_render_position)
{
    const glm::dvec3 up = glm::normalize(observer_render_position);
    return glm::dot(satellite_render_position - observer_render_position, up);
}

float satview_ground_marker_base_size(
    const glm::dvec3& satellite_render_position,
    const glm::dvec3& observer_render_position)
{
    constexpr float kMinimumMarkerSize = 0.008f;
    constexpr float kMaximumMarkerSize = 0.026f;
    constexpr float kOverheadLowOrbitRangeEarthRadii = 0.08f;
    constexpr float kLowOrbitHorizonRangeEarthRadii = 0.35f;
    const float observer_range = static_cast<float>(glm::length(satellite_render_position - observer_render_position));
    const float t = std::clamp(
        (observer_range - kOverheadLowOrbitRangeEarthRadii)
            / (kLowOrbitHorizonRangeEarthRadii - kOverheadLowOrbitRangeEarthRadii),
        0.0f,
        1.0f);
    return kMaximumMarkerSize + (kMinimumMarkerSize - kMaximumMarkerSize) * t;
}

std::size_t satview_ground_track_subdivision_count(
    const glm::dvec3& segment_start,
    const glm::dvec3& segment_end,
    const glm::dvec3& observer_render_position)
{
    const glm::dvec3 start_offset = segment_start - observer_render_position;
    const glm::dvec3 end_offset = segment_end - observer_render_position;
    const double start_length = glm::length(start_offset);
    const double end_length = glm::length(end_offset);
    if (!(start_length > kVectorEpsilon) || !(end_length > kVectorEpsilon))
        return kMaximumGroundTrackSubdivisions;

    const double cosine = std::clamp(
        glm::dot(start_offset, end_offset) / (start_length * end_length),
        -1.0,
        1.0);
    const double apparent_angle = std::acos(cosine);
    if (!std::isfinite(apparent_angle))
        return 1;
    return std::clamp<std::size_t>(
        static_cast<std::size_t>(std::ceil(apparent_angle / kMaximumGroundTrackSegmentAngle)),
        1,
        kMaximumGroundTrackSubdivisions);
}

glm::dvec3 satview_interpolate_track_arc(
    const glm::dvec3& segment_start,
    const glm::dvec3& segment_end,
    double interpolation)
{
    const double t = std::clamp(interpolation, 0.0, 1.0);
    const double start_radius = glm::length(segment_start);
    const double end_radius = glm::length(segment_end);
    if (!(start_radius > kVectorEpsilon) || !(end_radius > kVectorEpsilon))
        return glm::mix(segment_start, segment_end, t);

    const glm::dvec3 start_direction = segment_start / start_radius;
    const glm::dvec3 end_direction = segment_end / end_radius;
    const double cosine = std::clamp(glm::dot(start_direction, end_direction), -1.0, 1.0);
    glm::dvec3 direction;
    if (cosine > 0.9995)
    {
        direction = glm::normalize(glm::mix(start_direction, end_direction, t));
    }
    else if (cosine < -0.9995)
    {
        const glm::dvec3 reference = std::abs(start_direction.x) < 0.9
            ? glm::dvec3(1.0, 0.0, 0.0)
            : glm::dvec3(0.0, 1.0, 0.0);
        const glm::dvec3 orthogonal = glm::normalize(glm::cross(start_direction, reference));
        direction = start_direction * std::cos(std::numbers::pi_v<double> * t)
            + orthogonal * std::sin(std::numbers::pi_v<double> * t);
    }
    else
    {
        const double angle = std::acos(cosine);
        const double sine = std::sin(angle);
        if (std::abs(sine) <= kVectorEpsilon)
            direction = glm::normalize(glm::mix(start_direction, end_direction, t));
        else
            direction = (
                start_direction * std::sin((1.0 - t) * angle)
                + end_direction * std::sin(t * angle))
                / sine;
    }
    return direction * std::lerp(start_radius, end_radius, t);
}

SatViewGroundBodyProxy satview_ground_body_proxy(
    const glm::dvec3& body_render_position,
    double body_radius_earth_radii,
    const glm::dvec3& observer_render_position,
    double proxy_distance_earth_radii)
{
    const glm::dvec3 observer_to_body = body_render_position - observer_render_position;
    const double body_distance = glm::length(observer_to_body);
    if (!(body_radius_earth_radii > 0.0)
        || !(proxy_distance_earth_radii > 0.0)
        || !(body_distance > body_radius_earth_radii))
    {
        return {};
    }

    const double radius_scale = proxy_distance_earth_radii / body_distance;
    return {
        observer_render_position
            + observer_to_body * radius_scale,
        body_radius_earth_radii * radius_scale,
    };
}

} // namespace draxul::satview
