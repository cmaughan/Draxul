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

layout(set = 0, binding = 4) uniform sampler2D moon_tex;

void main()
{
    vec2 uv = vec2(fract(in_uv.x), 1.0 - clamp(in_uv.y, 0.0, 1.0));
    vec3 surface = texture(moon_tex, uv).rgb;
    vec3 normal = normalize(in_normal);
    vec3 sunlight_direction = normalize(push.sun_dir_time.xyz);
    vec3 earth_direction = -normalize(push.camera_orientation.xyz);
    float diffuse = max(dot(normal, sunlight_direction), 0.0);
    float earth_phase = 0.5 * (1.0 - dot(earth_direction, sunlight_direction));
    float earthshine = max(dot(normal, earth_direction), 0.0) * earth_phase * 0.055;
    float illumination = 0.006 + diffuse * 1.12 + earthshine;
    out_color = vec4(surface * illumination, 1.0);
}
