#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

#include "satview_sky_projection.glsl"

layout(location = 0) in vec4 in_position0_size;
layout(location = 1) in vec4 in_position1_selected;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec4 in_style;
layout(location = 4) in vec4 in_surface_normal;

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

vec2 marker_endpoint(int style, int segment, int endpoint)
{
    float sign_value = endpoint == 0 ? -1.0 : 1.0;
    if (style == 1)
    {
        if (segment == 0)
            return endpoint == 0 ? vec2(-0.9, 0.65) : vec2(0.9, 0.65);
        if (segment == 1)
            return endpoint == 0 ? vec2(0.9, 0.65) : vec2(0.0, -0.75);
        if (segment == 2)
            return endpoint == 0 ? vec2(0.0, -0.75) : vec2(-0.9, 0.65);
        return endpoint == 0 ? vec2(0.0, -0.75) : vec2(0.0, -1.2);
    }
    if (style == 2)
    {
        if (segment == 0)
            return endpoint == 0 ? vec2(-0.9, -0.7) : vec2(0.9, -0.7);
        if (segment == 1)
            return endpoint == 0 ? vec2(0.9, -0.7) : vec2(0.9, 0.7);
        if (segment == 2)
            return endpoint == 0 ? vec2(0.9, 0.7) : vec2(-0.9, 0.7);
        return endpoint == 0 ? vec2(-0.9, 0.7) : vec2(-0.9, -0.7);
    }
    if (style == 3)
    {
        if (segment == 0)
            return endpoint == 0 ? vec2(0.0, -1.0) : vec2(1.0, 0.0);
        if (segment == 1)
            return endpoint == 0 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
        if (segment == 2)
            return endpoint == 0 ? vec2(0.0, 1.0) : vec2(-1.0, 0.0);
        return endpoint == 0 ? vec2(-1.0, 0.0) : vec2(0.0, -1.0);
    }
    vec2 axis = segment == 0 ? vec2(1.0, 0.0)
        : segment == 1 ? vec2(0.0, 1.0)
        : segment == 2 ? vec2(1.0, 1.0) * 0.70710678
        : vec2(1.0, -1.0) * 0.70710678;
    return axis * sign_value;
}

void main()
{
    int segment = gl_VertexIndex / 2;
    int endpoint = gl_VertexIndex & 1;
    float selected = in_position1_selected.w;
    int style = int(round(in_style.x));
    vec2 marker_offset = marker_endpoint(style, segment, endpoint);
    float alpha = clamp(push.render_params.w, 0.0, 1.0);

    vec3 center = mix(in_position0_size.xyz, in_position1_selected.xyz, alpha);
    out_color = in_color;
    if (style == 0 && segment >= 2 && selected < 0.5)
        out_color.a = 0.0;

    if (push.camera_pos.w < 0.0)
    {
        vec3 body_position = push.camera_orientation.z > 1.5
            ? render_to_solar_body(center)
            : push.camera_orientation.z > 0.5
            ? render_to_lunar_body(center)
            : render_teme_to_ecef(center, push.render_params.z);
        vec3 local = ecef_to_map_local(body_position, push.camera_orientation.xy);
        float radius = max(length(local), 0.000001);
        float longitude = atan(local.y, local.x);
        float latitude = asin(clamp(local.z / radius, -1.0, 1.0));
        vec2 map_center = vec2(longitude / PI, 2.0 * latitude / PI);
        float x_scale = abs(push.camera_pos.w);
        map_center.x += in_style.y;
        vec2 map_position = map_center
            + marker_offset * vec2(x_scale, 1.0) * in_position0_size.w * 0.75;
        gl_Position = push.view_proj * vec4(map_position, 0.2, 1.0);
    }
    else if (satview_is_ground_projection())
    {
        vec4 center_clip = satview_project_world_position(center);
        vec2 center_ndc = center_clip.xy / max(center_clip.w, 0.000001);
        if (!satview_ground_world_position_visible(center)
            || center_clip.w <= 0.0
            || abs(center_ndc.x) > 1.5
            || abs(center_ndc.y) > 1.5)
        {
            out_color.a = 0.0;
            gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
            return;
        }
        float x_scale = satview_ground_projection_aspect_scale();
        center_clip.xy += marker_offset * vec2(x_scale, 1.0)
            * in_position0_size.w * 0.75 * center_clip.w;
        gl_Position = center_clip;
    }
    else
    {
        vec3 camera_right = normalize(
            rotate_by_quaternion(vec3(1.0, 0.0, 0.0), push.camera_orientation));
        vec3 camera_up = normalize(
            rotate_by_quaternion(vec3(0.0, 1.0, 0.0), push.camera_orientation));
        vec3 right = camera_right;
        vec3 up = camera_up;
        bool surface_aligned = in_surface_normal.w > 0.5;
        if (surface_aligned)
        {
            vec3 normal = normalize(in_surface_normal.xyz);
            vec3 to_camera = push.camera_pos.xyz - center;
            if (dot(normal, to_camera) <= 0.0)
            {
                out_color.a = 0.0;
                gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
                return;
            }

            vec3 projected_up = camera_up - normal * dot(camera_up, normal);
            if (dot(projected_up, projected_up) > 0.000001)
            {
                up = normalize(projected_up);
                right = normalize(cross(up, normal));
            }
            else
            {
                right = normalize(camera_right - normal * dot(camera_right, normal));
                up = normalize(cross(normal, right));
            }
        }

        vec3 world = center + (right * marker_offset.x + up * marker_offset.y)
            * in_position0_size.w;
        gl_Position = push.view_proj * vec4(world, 1.0);
        if (surface_aligned)
            gl_Position.z = min(gl_Position.w, gl_Position.z + 0.00002 * gl_Position.w);
    }
}
