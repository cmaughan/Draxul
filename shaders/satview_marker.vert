#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) in vec4 in_position0_size;
layout(location = 1) in vec4 in_position1_selected;
layout(location = 2) in vec4 in_color;

layout(location = 0) out vec4 out_color;

const float PI = 3.14159265358979323846;

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

void main()
{
    int segment = gl_VertexIndex / 2;
    int endpoint = gl_VertexIndex & 1;
    float endpoint_sign = endpoint == 0 ? -1.0 : 1.0;
    float selected = in_position1_selected.w;
    float alpha = clamp(push.render_params.w, 0.0, 1.0);

    vec3 center = mix(in_position0_size.xyz, in_position1_selected.xyz, alpha);
    out_color = in_color;
    if (segment >= 2 && selected < 0.5)
        out_color.a = 0.0;

    if (push.camera_pos.w < 0.0)
    {
        vec3 ecef = render_teme_to_ecef(center, push.render_params.z);
        vec3 local = ecef_to_map_local(ecef, push.camera_orientation.xy);
        float radius = max(length(local), 0.000001);
        float longitude = atan(local.y, local.x);
        float latitude = asin(clamp(local.z / radius, -1.0, 1.0));
        vec2 map_center = vec2(longitude / PI, 2.0 * latitude / PI);
        float x_scale = abs(push.camera_pos.w);
        vec2 map_axis = segment == 0 ? vec2(x_scale, 0.0)
            : segment == 1 ? vec2(0.0, 1.0)
            : segment == 2 ? vec2(x_scale, 1.0) * 0.70710678
            : vec2(x_scale, -1.0) * 0.70710678;
        vec2 map_position = map_center
            + map_axis * in_position0_size.w * 0.75 * endpoint_sign;
        gl_Position = push.view_proj * vec4(map_position, 0.2, 1.0);
    }
    else
    {
        vec3 right = normalize(rotate_by_quaternion(vec3(1.0, 0.0, 0.0), push.camera_orientation));
        vec3 up = normalize(rotate_by_quaternion(vec3(0.0, 1.0, 0.0), push.camera_orientation));
        vec3 axis = segment == 0 ? right
            : segment == 1 ? up
            : segment == 2 ? normalize(right + up)
            : normalize(right - up);
        vec3 world = center + axis * in_position0_size.w * endpoint_sign;
        gl_Position = push.view_proj * vec4(world, 1.0);
    }
}
