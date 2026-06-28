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

layout(set = 0, binding = 2) uniform sampler2D earth_cloud_tex;
layout(set = 0, binding = 3) uniform sampler2D earth_live_cloud_tex;

const float CLOUD_RADIUS = 1.0015;

vec2 ray_sphere(vec3 origin, vec3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0)
        return vec2(-1.0);
    float root = sqrt(discriminant);
    return vec2(-b - root, -b + root);
}

void main()
{
    float cloud_mode = push.sun_dir_time.w;
    if (cloud_mode < 0.5)
        discard;

    vec3 ray_origin = push.camera_pos.xyz;
    vec3 ray_direction = normalize(in_world - ray_origin);
    vec2 shell_hit = ray_sphere(ray_origin, ray_direction, CLOUD_RADIUS);
    float fragment_distance = length(in_world - ray_origin);
    if (fragment_distance > 0.5 * (shell_hit.x + shell_hit.y))
        discard;

    vec2 uv = vec2(fract(in_uv.x), 1.0 - clamp(in_uv.y, 0.0, 1.0));
    vec3 bundled_cloud_sample = texture(earth_cloud_tex, uv).rgb;
    vec3 live_cloud_sample = texture(earth_live_cloud_tex, uv).rgb;
    vec3 cloud_sample = mix(
        bundled_cloud_sample, live_cloud_sample, clamp(cloud_mode - 1.0, 0.0, 1.0));
    float opacity = smoothstep(
        0.20, 0.78, dot(cloud_sample, vec3(0.299, 0.587, 0.114)));
    if (opacity < 0.002)
        discard;

    vec3 normal = normalize(in_normal);
    vec3 light = normalize(push.sun_dir_time.xyz);
    float ndl = dot(normal, light);
    float daylight = smoothstep(-0.10, 0.16, ndl);
    float diffuse = max(ndl, 0.0);
    vec3 day_color = vec3(0.91, 0.95, 1.0) * (0.34 + diffuse * 0.76);
    vec3 night_color = vec3(0.025, 0.030, 0.042);
    vec3 cloud_color = mix(night_color, day_color, daylight);
    float alpha = opacity * mix(0.32, 0.78, daylight);
    out_color = vec4(cloud_color * alpha, alpha);
}
