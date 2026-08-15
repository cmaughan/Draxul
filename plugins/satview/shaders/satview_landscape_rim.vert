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

layout(location = 0) in vec4 in_local_direction0;
layout(location = 1) in vec4 in_local_direction1;
layout(location = 2) in vec4 in_color;
layout(location = 0) out vec4 out_color;

const float LANDSCAPE_DISTANCE_EARTH_RADII = 48.0;

vec3 landscape_world_direction(vec3 local_direction)
{
    vec3 up = normalize(push.camera_pos.xyz);
    vec3 east = cross(vec3(0.0, 1.0, 0.0), up);
    if (dot(east, east) < 1.0e-8)
        east = vec3(1.0, 0.0, 0.0);
    east = normalize(east);
    vec3 north = normalize(cross(up, east));
    return normalize(
        east * local_direction.x
        + north * local_direction.y
        + up * local_direction.z);
}

vec4 project_landscape_direction(vec3 direction)
{
    return satview_project_world_position(
        push.camera_pos.xyz + direction * LANDSCAPE_DISTANCE_EARTH_RADII);
}

bool landscape_projection_valid(vec3 direction, vec4 projected)
{
    vec2 ndc = projected.xy / max(projected.w, 0.000001);
    bool valid = projected.w > 0.0
        && !any(isnan(ndc))
        && !any(isinf(ndc))
        && max(abs(ndc.x), abs(ndc.y)) < 8.0;
    if (satview_is_stereographic_ground_projection())
    {
        vec3 camera_direction = mat3(push.view_proj) * direction;
        valid = valid && (1.0 - camera_direction.z) > 0.02;
    }
    return valid;
}

void main()
{
    vec3 directions[2] = vec3[2](
        landscape_world_direction(in_local_direction0.xyz),
        landscape_world_direction(in_local_direction1.xyz));
    vec4 projected[2] = vec4[2](
        project_landscape_direction(directions[0]),
        project_landscape_direction(directions[1]));
    bool primitive_visible = landscape_projection_valid(directions[0], projected[0])
        && landscape_projection_valid(directions[1], projected[1]);
    out_color = primitive_visible ? in_color : vec4(0.0);
    gl_Position = primitive_visible
        ? projected[gl_VertexIndex]
        : vec4(2.0, 2.0, 0.0, 1.0);
}
