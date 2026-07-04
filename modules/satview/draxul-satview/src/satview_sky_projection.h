#pragma once

#include <draxul/satview/satview_config.h>
#include <glm/glm.hpp>

namespace draxul::satview
{

struct SatViewSkyProjectionPoint
{
    glm::vec2 ndc{ 0.0f };
    bool valid = false;
};

[[nodiscard]] float satview_maximum_ground_fov_degrees(SatViewGroundProjection projection);
[[nodiscard]] float satview_clamp_ground_fov_degrees(
    SatViewGroundProjection projection,
    float fov_degrees);
[[nodiscard]] SatViewSkyProjectionPoint satview_project_camera_direction(
    SatViewGroundProjection projection,
    glm::vec3 camera_direction,
    float vertical_fov_degrees,
    float viewport_aspect);
[[nodiscard]] glm::vec3 satview_unproject_ground_ndc(
    SatViewGroundProjection projection,
    glm::vec2 ndc,
    float vertical_fov_degrees,
    float viewport_aspect);
[[nodiscard]] glm::mat4 satview_stereographic_frame_transform(
    const glm::mat4& view,
    float vertical_fov_degrees,
    float viewport_aspect);

} // namespace draxul::satview
