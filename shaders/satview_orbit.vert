#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) in vec4 in_position;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 out_color;

void main()
{
    out_color = in_color;
    gl_Position = push.view_proj * vec4(in_position.xyz, 1.0);
}
