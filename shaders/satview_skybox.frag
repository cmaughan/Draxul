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

layout(binding = 6) uniform sampler2D milky_way_tex;

layout(location = 0) in vec2 in_ndc;
layout(location = 0) out vec4 out_color;

const float PI = 3.14159265358979323846;

void main()
{
    vec3 direction = satview_ground_ray_direction(in_ndc);
    if (satview_is_ground_projection() && satview_ground_horizon_occlusion_enabled())
    {
        vec3 observer_up = normalize(push.camera_pos.xyz);
        float observer_radius_sq = max(dot(push.camera_pos.xyz, push.camera_pos.xyz), 1.0);
        float horizon_cosine = -sqrt(max(1.0 - 1.0 / observer_radius_sq, 0.0));
        if (dot(direction, observer_up) <= horizon_cosine)
            discard;
    }

    // Inverse of SatView's ICRF-to-render basis: eq = (-z, -x, y).
    float right_ascension = atan(-direction.x, -direction.z);
    float declination = asin(clamp(direction.y, -1.0, 1.0));
    vec2 uv = vec2(
        fract(0.5 - right_ascension / (2.0 * PI)),
        0.5 - declination / PI);
    out_color = vec4(
        texture(milky_way_tex, uv).rgb * clamp(push.render_params.w, 0.0, 1.0),
        1.0);
}
