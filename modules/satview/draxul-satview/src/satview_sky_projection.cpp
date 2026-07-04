#include "satview_sky_projection.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace draxul::satview
{

namespace
{

constexpr float kMinimumGroundFovDegrees = 20.0f;
constexpr float kMaximumPerspectiveFovDegrees = 120.0f;
constexpr float kMaximumStereographicFovDegrees = 235.0f;
constexpr float kProjectionEpsilon = 1.0e-6f;

float safe_aspect(float viewport_aspect)
{
    return std::max(viewport_aspect, kProjectionEpsilon);
}

float perspective_scale(float vertical_fov_degrees)
{
    return std::tan(0.5f * glm::radians(vertical_fov_degrees));
}

float stereographic_scale(float vertical_fov_degrees)
{
    return std::tan(0.25f * glm::radians(vertical_fov_degrees));
}

} // namespace

float satview_maximum_ground_fov_degrees(SatViewGroundProjection projection)
{
    return projection == SatViewGroundProjection::Perspective
        ? kMaximumPerspectiveFovDegrees
        : kMaximumStereographicFovDegrees;
}

float satview_clamp_ground_fov_degrees(
    SatViewGroundProjection projection,
    float fov_degrees)
{
    return std::clamp(
        fov_degrees,
        kMinimumGroundFovDegrees,
        satview_maximum_ground_fov_degrees(projection));
}

SatViewSkyProjectionPoint satview_project_camera_direction(
    SatViewGroundProjection projection,
    glm::vec3 camera_direction,
    float vertical_fov_degrees,
    float viewport_aspect)
{
    const float direction_length = glm::length(camera_direction);
    if (!(direction_length > kProjectionEpsilon))
        return {};

    camera_direction /= direction_length;
    const float aspect = safe_aspect(viewport_aspect);
    if (projection == SatViewGroundProjection::Perspective)
    {
        if (camera_direction.z >= -kProjectionEpsilon)
            return {};
        const float scale = perspective_scale(satview_clamp_ground_fov_degrees(
            projection, vertical_fov_degrees));
        return {
            glm::vec2(
                camera_direction.x / (-camera_direction.z * scale * aspect),
                camera_direction.y / (-camera_direction.z * scale)),
            true,
        };
    }

    const float denominator = 1.0f - camera_direction.z;
    if (denominator <= kProjectionEpsilon)
        return {};
    const float scale = stereographic_scale(satview_clamp_ground_fov_degrees(
        projection, vertical_fov_degrees));
    const glm::vec2 plane = glm::vec2(camera_direction) / denominator;
    return {
        glm::vec2(plane.x / (scale * aspect), plane.y / scale),
        true,
    };
}

glm::vec3 satview_unproject_ground_ndc(
    SatViewGroundProjection projection,
    glm::vec2 ndc,
    float vertical_fov_degrees,
    float viewport_aspect)
{
    const float aspect = safe_aspect(viewport_aspect);
    const float clamped_fov = satview_clamp_ground_fov_degrees(
        projection, vertical_fov_degrees);
    if (projection == SatViewGroundProjection::Perspective)
    {
        const float scale = perspective_scale(clamped_fov);
        return glm::normalize(glm::vec3(
            ndc.x * scale * aspect,
            ndc.y * scale,
            -1.0f));
    }

    const float scale = stereographic_scale(clamped_fov);
    const glm::vec2 plane(ndc.x * scale * aspect, ndc.y * scale);
    const float radius_sq = glm::dot(plane, plane);
    const float inverse_denominator = 1.0f / (1.0f + radius_sq);
    return glm::vec3(
        2.0f * plane.x * inverse_denominator,
        2.0f * plane.y * inverse_denominator,
        (radius_sq - 1.0f) * inverse_denominator);
}

glm::mat4 satview_stereographic_frame_transform(
    const glm::mat4& view,
    float vertical_fov_degrees,
    float viewport_aspect)
{
    glm::mat4 transform{ glm::mat3(view) };
    transform[3][2] = stereographic_scale(satview_clamp_ground_fov_degrees(
        SatViewGroundProjection::Stereographic,
        vertical_fov_degrees));
    transform[3][3] = 1.0f / safe_aspect(viewport_aspect);
    return transform;
}

} // namespace draxul::satview
