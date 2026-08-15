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

vec3 body_color(int body_id)
{
    if (body_id == 6) return vec3(0.48, 0.43, 0.38);
    if (body_id == 7) return vec3(0.58, 0.54, 0.49);
    if (body_id == 9) return vec3(0.91, 0.76, 0.30);
    if (body_id == 10) return vec3(0.76, 0.69, 0.57);
    if (body_id == 11) return vec3(0.56, 0.51, 0.44);
    if (body_id == 12) return vec3(0.39, 0.36, 0.32);
    if (body_id == 14) return vec3(0.70, 0.70, 0.68);
    if (body_id == 15) return vec3(0.85, 0.88, 0.90);
    if (body_id == 16) return vec3(0.75, 0.76, 0.76);
    if (body_id == 17) return vec3(0.72, 0.73, 0.74);
    if (body_id == 18) return vec3(0.67, 0.67, 0.66);
    if (body_id == 19) return vec3(0.80, 0.55, 0.20);
    if (body_id == 20) return vec3(0.52, 0.48, 0.43);
    if (body_id == 22) return vec3(0.69, 0.70, 0.70);
    if (body_id == 23) return vec3(0.72, 0.74, 0.75);
    if (body_id == 24) return vec3(0.37, 0.37, 0.38);
    if (body_id == 25) return vec3(0.62, 0.62, 0.62);
    if (body_id == 26) return vec3(0.50, 0.47, 0.45);
    if (body_id == 28) return vec3(0.70, 0.64, 0.60);
    return vec3(0.72, 0.74, 0.78);
}

void main()
{
    const vec3 normal = normalize(in_normal);
    const vec3 light = normalize(push.sun_dir_time.xyz);
    const vec3 view = normalize(push.camera_pos.xyz - in_world);
    const float diffuse = max(dot(normal, light), 0.0);
    const float rim = pow(1.0 - max(dot(normal, view), 0.0), 3.0);
    const int body_id = int(push.sun_dir_time.w + 0.5);
    const vec3 surface = body_color(body_id);
    out_color = vec4(surface * (0.035 + 1.10 * diffuse) + surface * rim * 0.08, 1.0);
}
