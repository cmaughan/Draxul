#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) in vec2 in_local;
layout(location = 0) out vec4 out_color;

void main()
{
    const float inner_radius = push.render_params.x;
    const float outer_radius = push.camera_orientation.w;
    const float radius = length(in_local);
    if (radius < inner_radius || radius > outer_radius)
        discard;

    out_color = vec4(push.sun_dir_time.rgb, 1.0);
}
