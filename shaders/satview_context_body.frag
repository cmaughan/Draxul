#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_world;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 9) uniform sampler2D context_body_tex;

void main()
{
    const vec2 uv = vec2(fract(in_uv.x), 1.0 - clamp(in_uv.y, 0.0, 1.0));
    const vec3 surface = texture(context_body_tex, uv).rgb;
    const vec3 normal = normalize(in_normal);
    const vec3 light = normalize(push.sun_dir_time.xyz);
    const vec3 view = normalize(push.camera_pos.xyz - in_world);
    const float diffuse = max(dot(normal, light), 0.0);
    const float rim = pow(1.0 - max(dot(normal, view), 0.0), 3.0);
    out_color = vec4(
        surface * (0.025 + 1.12 * diffuse) + surface * rim * 0.025,
        1.0);
}
