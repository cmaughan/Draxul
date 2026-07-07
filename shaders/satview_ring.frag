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
    const float inner_radius = push.camera_orientation.x;
    const float outer_radius = push.camera_orientation.y;
    const float radius = length(in_local);
    if (radius < inner_radius || radius > outer_radius)
        discard;

    const float band_width = max(outer_radius - inner_radius, 0.001);
    const float feather = min(0.035, band_width * 0.45);
    const float inner_alpha = smoothstep(inner_radius, inner_radius + feather, radius);
    const float outer_alpha = 1.0 - smoothstep(outer_radius - feather, outer_radius, radius);
    const float alpha = push.sun_dir_time.a * inner_alpha * outer_alpha;
    out_color = vec4(push.sun_dir_time.rgb, alpha);
}
