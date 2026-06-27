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

layout(set = 0, binding = 0) uniform sampler2D earth_day_tex;
layout(set = 0, binding = 1) uniform sampler2D earth_night_tex;
layout(set = 0, binding = 2) uniform sampler2D earth_cloud_tex;

void main()
{
    vec3 n = normalize(in_normal);
    vec3 light = normalize(push.sun_dir_time.xyz);
    vec3 view = normalize(push.camera_pos.xyz - in_world);
    vec2 uv = vec2(fract(in_uv.x), 1.0 - clamp(in_uv.y, 0.0, 1.0));

    vec3 day_surface = texture(earth_day_tex, uv).rgb;
    vec3 night_surface = texture(earth_night_tex, uv).rgb;
    vec3 cloud_sample = texture(earth_cloud_tex, uv + vec2(push.sun_dir_time.w * 0.0000007, 0.0)).rgb;
    float cloud_alpha = smoothstep(0.20, 0.78, dot(cloud_sample, vec3(0.299, 0.587, 0.114)));

    float ndl = dot(n, light);
    float day = smoothstep(-0.08, 0.14, ndl);
    float diffuse = max(ndl, 0.0);
    vec3 lit = day_surface * (0.22 + diffuse * 1.08);
    float ocean_hint = smoothstep(0.03, 0.24, day_surface.b - max(day_surface.r, day_surface.g));
    float specular = pow(max(dot(reflect(-light, n), view), 0.0), 48.0) * ocean_hint * smoothstep(0.0, 0.25, ndl);
    lit += vec3(0.55, 0.72, 0.90) * specular * 0.30;

    vec3 night = night_surface * 1.85 + day_surface * 0.015;
    vec3 cloud_day = vec3(0.92, 0.96, 1.0) * (0.28 + diffuse * 0.92);
    vec3 cloud_night = vec3(0.045, 0.050, 0.065);
    lit = mix(lit, cloud_day, cloud_alpha * 0.58);
    night = mix(night, max(night, cloud_night), cloud_alpha * 0.35);

    float rim = pow(1.0 - max(dot(view, n), 0.0), 3.0);
    vec3 atmosphere = vec3(0.12, 0.36, 0.66) * rim * (0.45 + day * 0.65);
    vec3 color = mix(night, lit, day) + atmosphere;
    out_color = vec4(color, 1.0);
}
