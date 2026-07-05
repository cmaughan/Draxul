#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

#include "satview_sky_projection.glsl"

layout(location = 0) in vec4 in_direction;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec4 in_paired_direction;

layout(location = 0) out vec4 out_color;

const float CONSTELLATION_DISTANCE_EARTH_RADII = 48.0;

void main()
{
    vec3 world_position = push.camera_pos.xyz
        + normalize(in_direction.xyz) * CONSTELLATION_DISTANCE_EARTH_RADII;
    vec3 paired_world_position = push.camera_pos.xyz
        + normalize(in_paired_direction.xyz) * CONSTELLATION_DISTANCE_EARTH_RADII;
    vec4 projected = satview_project_world_position(world_position);
    vec4 paired_projected = satview_project_world_position(paired_world_position);
    bool visible = satview_ground_world_position_visible(world_position)
        && satview_ground_world_position_visible(paired_world_position)
        && projected.w > 0.0
        && paired_projected.w > 0.0;
    if (visible && satview_is_stereographic_ground_projection())
    {
        vec2 ndc = projected.xy / projected.w;
        vec2 paired_ndc = paired_projected.xy / paired_projected.w;
        visible = distance(ndc, paired_ndc) < 8.0;
    }

    out_color = visible ? in_color : vec4(0.0);
    gl_Position = visible ? projected : vec4(2.0, 2.0, 0.0, 1.0);
}
