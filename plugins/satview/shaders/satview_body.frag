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

layout(set = 0, binding = 8) uniform sampler2D body_tex;

const float PI = 3.14159265358979323846;

vec3 map_local_to_body(vec3 local, vec2 center)
{
    const float cos_longitude = cos(center.x);
    const float sin_longitude = sin(center.x);
    const float cos_latitude = cos(center.y);
    const float sin_latitude = sin(center.y);
    const vec3 center_axis = vec3(
        cos_latitude * cos_longitude,
        cos_latitude * sin_longitude,
        sin_latitude);
    const vec3 east_axis = vec3(-sin_longitude, cos_longitude, 0.0);
    const vec3 north_axis = vec3(
        -sin_latitude * cos_longitude,
        -sin_latitude * sin_longitude,
        cos_latitude);
    return local.x * center_axis + local.y * east_axis + local.z * north_axis;
}

void main()
{
    const bool map_projection = push.camera_pos.w < 0.0;
    vec2 uv = vec2(fract(in_uv.x), 1.0 - clamp(in_uv.y, 0.0, 1.0));
    vec3 normal = normalize(in_normal);
    if (map_projection)
    {
        const float longitude = (in_uv.x - 0.5) * 2.0 * PI;
        const float latitude = mix(-0.5 * PI, 0.5 * PI, in_uv.y);
        const float cp = cos(latitude);
        const vec3 body = map_local_to_body(
            vec3(cp * cos(longitude), cp * sin(longitude), sin(latitude)),
            push.camera_orientation.xy);
        normal = normalize(vec3(body.y, body.z, body.x));
        uv = vec2(
            fract(atan(body.y, body.x) / (2.0 * PI) + 0.5),
            0.5 - asin(clamp(body.z, -1.0, 1.0)) / PI);
    }

    const vec3 surface = texture(body_tex, uv).rgb;
    if (push.sun_dir_time.w < 0.0)
    {
        const vec3 emission = surface * (map_projection ? 0.72 : 2.05);
        out_color = vec4(emission, 1.0);
        return;
    }

    const vec3 light = normalize(push.sun_dir_time.xyz);
    const vec3 view = map_projection ? normal : normalize(push.camera_pos.xyz - in_world);
    const float diffuse = max(dot(normal, light), 0.0);
    const float rim = pow(1.0 - max(dot(normal, view), 0.0), 3.0);
    const vec3 color = surface * (0.025 + 1.12 * diffuse)
        + surface * rim * 0.025;
    out_color = vec4(color, 1.0);
}
