#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) in vec2 in_ndc;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D earth_day_tex;
layout(set = 0, binding = 1) uniform sampler2D earth_night_tex;
layout(set = 0, binding = 2) uniform sampler2D earth_cloud_tex;
layout(set = 0, binding = 3) uniform sampler2D earth_live_cloud_tex;

const float PI = 3.14159265358979323846;
const float PLANET_RADIUS = 1.0;

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

vec3 ground_ray_direction()
{
    mat4 inv_view_proj = inverse(push.view_proj);
    vec4 world_far = inv_view_proj * vec4(in_ndc, 1.0, 1.0);
    world_far /= world_far.w;
    return normalize(world_far.xyz - push.camera_pos.xyz);
}

void main()
{
    vec3 ray_origin = push.camera_pos.xyz;
    vec3 ray_direction = ground_ray_direction();
    vec3 up = normalize(ray_origin);
    float observer_radius_sq = max(dot(ray_origin, ray_origin), PLANET_RADIUS * PLANET_RADIUS);
    float horizon_cosine = -sqrt(max(1.0 - (PLANET_RADIUS * PLANET_RADIUS) / observer_radius_sq, 0.0));
    float ground_alpha = 1.0 - smoothstep(horizon_cosine - 0.0015, horizon_cosine + 0.0015, dot(ray_direction, up));
    if (ground_alpha <= 0.001)
        discard;

    vec2 planet_hit = ray_sphere(ray_origin, ray_direction, PLANET_RADIUS);
    float hit_distance = planet_hit.x > 0.0 ? planet_hit.x : planet_hit.y;
    if (hit_distance <= 0.0)
        discard;

    vec3 world = ray_origin + ray_direction * hit_distance;
    vec3 n = normalize(world);
    vec3 ecef = normalize(render_teme_to_ecef(n, push.render_params.z));
    float longitude = atan(ecef.y, ecef.x);
    float latitude = asin(clamp(ecef.z, -1.0, 1.0));
    vec2 uv = vec2(
        fract(longitude / (2.0 * PI) + 0.5),
        0.5 - latitude / PI);

    vec3 light = normalize(push.sun_dir_time.xyz);
    vec3 view = normalize(push.camera_pos.xyz - world);
    vec3 day_surface = texture(earth_day_tex, uv).rgb;
    vec3 night_surface = texture(earth_night_tex, uv).rgb;
    float ndl = dot(n, light);
    float day = smoothstep(-0.08, 0.14, ndl);
    float diffuse = max(ndl, 0.0);
    vec3 lit = day_surface * (0.22 + diffuse * 1.08);
    float ocean_hint = smoothstep(0.03, 0.24, day_surface.b - max(day_surface.r, day_surface.g));
    float specular = pow(max(dot(reflect(-light, n), view), 0.0), 48.0) * ocean_hint * smoothstep(0.0, 0.25, ndl);
    lit += vec3(0.55, 0.72, 0.90) * specular * 0.30;

    vec3 night = night_surface * 1.85 + day_surface * 0.015;
    vec3 color = mix(night, lit, day);
    if (push.sun_dir_time.w > 0.5)
    {
        vec3 bundled_cloud_sample = texture(earth_cloud_tex, uv).rgb;
        vec3 live_cloud_sample = texture(earth_live_cloud_tex, uv).rgb;
        vec3 cloud_sample = mix(
            bundled_cloud_sample,
            live_cloud_sample,
            clamp(push.sun_dir_time.w - 1.0, 0.0, 1.0));
        float opacity = smoothstep(
            0.20, 0.78, dot(cloud_sample, vec3(0.299, 0.587, 0.114)));
        float daylight = smoothstep(-0.10, 0.16, ndl);
        vec3 day_cloud = vec3(0.91, 0.95, 1.0) * (0.34 + diffuse * 0.76);
        vec3 night_cloud = vec3(0.025, 0.030, 0.042);
        vec3 cloud_color = mix(night_cloud, day_cloud, daylight);
        float cloud_alpha = opacity * mix(0.32, 0.78, daylight);
        color = mix(color, cloud_color, cloud_alpha);
    }
    out_color = vec4(color * ground_alpha, ground_alpha);
}
