#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) in vec4 in_direction_magnitude;
layout(location = 1) in vec4 in_color_size;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec2 out_uv;

const float STAR_DISTANCE_EARTH_RADII = 48.0;

vec2 quad_corner(uint corner_index)
{
    switch (corner_index)
    {
    case 0:
        return vec2(-1.0, -1.0);
    case 1:
        return vec2(-1.0, 1.0);
    case 2:
        return vec2(1.0, -1.0);
    case 3:
        return vec2(1.0, -1.0);
    case 4:
        return vec2(-1.0, 1.0);
    default:
        return vec2(1.0, 1.0);
    }
}

void main()
{
    vec3 direction = normalize(in_direction_magnitude.xyz);
    vec3 center = push.camera_pos.xyz + direction * STAR_DISTANCE_EARTH_RADII;
    vec4 center_clip = push.view_proj * vec4(center, 1.0);
    float contrast = max(push.render_params.x, 0.01);
    float magnitude_boost = pow(10.0, -0.4 * (in_direction_magnitude.w - 4.0) * (contrast - 1.0));
    float brightness_boost = clamp(magnitude_boost, 0.10, 4.0);
    float size_boost = clamp(pow(magnitude_boost, 0.25), 0.55, 1.7);
    out_color = vec4(in_color_size.rgb * brightness_boost, 1.0);

    if (center_clip.w <= 0.0)
    {
        out_color.a = 0.0;
        out_uv = vec2(2.0);
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    vec2 corner = quad_corner(uint(gl_VertexIndex));
    float x_scale = max(push.render_params.y, 0.0001);
    center_clip.xy += corner * vec2(x_scale, 1.0) * in_color_size.w * size_boost * center_clip.w;
    gl_Position = center_clip;
    out_uv = corner;
}
