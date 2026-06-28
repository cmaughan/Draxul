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
layout(location = 2) in vec4 in_paired_position;

layout(location = 0) out vec4 out_color;

const float PI = 3.14159265358979323846;

vec3 render_teme_to_ecef(vec3 render_position, float sidereal_angle)
{
    vec3 teme = vec3(-render_position.z, -render_position.x, render_position.y);
    float c = cos(sidereal_angle);
    float s = sin(sidereal_angle);
    return vec3(
        c * teme.x + s * teme.y,
        -s * teme.x + c * teme.y,
        teme.z);
}

vec3 ecef_to_map_local(vec3 ecef, vec2 center)
{
    float cos_longitude = cos(center.x);
    float sin_longitude = sin(center.x);
    float cos_latitude = cos(center.y);
    float sin_latitude = sin(center.y);
    vec3 center_axis = vec3(
        cos_latitude * cos_longitude,
        cos_latitude * sin_longitude,
        sin_latitude);
    vec3 east_axis = vec3(-sin_longitude, cos_longitude, 0.0);
    vec3 north_axis = vec3(
        -sin_latitude * cos_longitude,
        -sin_latitude * sin_longitude,
        cos_latitude);
    return vec3(
        dot(ecef, center_axis),
        dot(ecef, east_axis),
        dot(ecef, north_axis));
}

vec2 map_position(vec3 render_position)
{
    vec3 ecef = render_teme_to_ecef(render_position, push.render_params.z);
    vec3 local = ecef_to_map_local(ecef, push.camera_orientation.xy);
    float radius = max(length(local), 0.000001);
    return vec2(
        atan(local.y, local.x) / PI,
        2.0 * asin(clamp(local.z / radius, -1.0, 1.0)) / PI);
}

void main()
{
    out_color = in_color;
    if (push.camera_pos.w < 0.0)
    {
        vec2 projected = map_position(in_position.xyz);
        vec2 paired = map_position(in_paired_position.xyz);
        if (in_position.w > 0.0)
        {
            float delta = projected.x - paired.x;
            if (delta > 1.0)
                projected.x -= 2.0;
            else if (delta < -1.0)
                projected.x += 2.0;
        }
        projected.x += push.camera_pos.x;
        gl_Position = push.view_proj * vec4(projected, 0.4, 1.0);
    }
    else
        gl_Position = push.view_proj * vec4(in_position.xyz, 1.0);
}
