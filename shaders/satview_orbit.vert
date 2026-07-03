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
const vec3 LUNAR_NORTH_POLE_RENDER = vec3(
    0.39812155,
    0.91733267,
    0.00003544);

vec3 rotate_by_quaternion(vec3 value, vec4 quaternion)
{
    vec3 twice_cross = 2.0 * cross(quaternion.xyz, value);
    return value + quaternion.w * twice_cross + cross(quaternion.xyz, twice_cross);
}

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

vec3 render_to_lunar_body(vec3 render_position)
{
    vec3 far_axis = normalize(push.camera_pos.xyz);
    vec3 north_axis = normalize(
        LUNAR_NORTH_POLE_RENDER
        - far_axis * dot(LUNAR_NORTH_POLE_RENDER, far_axis));
    vec3 local_x_axis = normalize(cross(north_axis, far_axis));
    vec3 moon_relative = render_position - push.camera_pos.xyz;
    return vec3(
        dot(moon_relative, -far_axis),
        dot(moon_relative, -local_x_axis),
        dot(moon_relative, north_axis));
}

vec3 render_to_solar_body(vec3 render_position)
{
    vec4 render_to_body = vec4(-push.sun_dir_time.xyz, push.sun_dir_time.w);
    return rotate_by_quaternion(
        render_position - push.camera_pos.xyz,
        render_to_body);
}

vec2 map_position(vec3 render_position, bool earth_fixed)
{
    vec3 body_position = push.camera_orientation.z > 1.5
        ? render_to_solar_body(render_position)
        : push.camera_orientation.z > 0.5
        ? render_to_lunar_body(render_position)
        : earth_fixed
            ? render_position
            : render_teme_to_ecef(render_position, push.render_params.z);
    vec3 local = ecef_to_map_local(body_position, push.camera_orientation.xy);
    float radius = max(length(local), 0.000001);
    return vec2(
        atan(local.y, local.x) / PI,
        2.0 * asin(clamp(local.z / radius, -1.0, 1.0)) / PI);
}

void main()
{
    out_color = in_color;
    bool sun_centered = push.render_params.w < -0.5;
    vec3 track_center = push.camera_pos.w < 0.0
        ? push.camera_pos.xyz
        : push.camera_orientation.xyz;
    vec3 position = in_position.xyz + (sun_centered ? track_center : vec3(0.0));
    vec3 paired_position = in_paired_position.xyz
        + (sun_centered ? track_center : vec3(0.0));
    if (push.camera_pos.w < 0.0)
    {
        bool earth_fixed = abs(in_position.w) > 1.5;
        vec2 projected = map_position(position, earth_fixed);
        vec2 paired = map_position(paired_position, earth_fixed);
        if (in_position.w > 0.0)
        {
            float delta = projected.x - paired.x;
            if (delta > 1.0)
                projected.x -= 2.0;
            else if (delta < -1.0)
                projected.x += 2.0;
        }
        projected.x += push.camera_orientation.w;
        gl_Position = push.view_proj * vec4(projected, 0.4, 1.0);
    }
    else
        gl_Position = push.view_proj * vec4(position, 1.0);
}
