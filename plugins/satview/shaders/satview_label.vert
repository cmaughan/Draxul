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

layout(location = 0) in vec4 in_direction_priority;
layout(location = 1) in vec4 in_uv_rect;
layout(location = 2) in vec4 in_pixel_size_offset;
layout(location = 3) in vec4 in_color;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;

const float LABEL_DISTANCE_EARTH_RADII = 48.0;

vec2 quad_corner(uint vertex_index)
{
    const vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    return corners[vertex_index];
}

void main()
{
    vec3 world_position = push.camera_pos.xyz
        + normalize(in_direction_priority.xyz) * LABEL_DISTANCE_EARTH_RADII;
    vec4 projected = satview_project_world_position(world_position);
    bool visible = satview_ground_world_position_visible(world_position)
        && projected.w > 0.0;
    vec2 corner = quad_corner(gl_VertexIndex);
    vec2 viewport = max(push.render_params.xy, vec2(1.0));
    vec2 pixel_offset = corner * in_pixel_size_offset.xy * 0.5 + in_pixel_size_offset.zw;
    projected.xy += pixel_offset * (2.0 / viewport) * projected.w;
    out_uv = mix(in_uv_rect.xy, in_uv_rect.zw, corner * 0.5 + 0.5);
    out_color = visible ? in_color : vec4(0.0);
    gl_Position = visible ? projected : vec4(2.0, 2.0, 0.0, 1.0);
}
