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
layout(set = 0, binding = 3) uniform sampler2D earth_live_cloud_tex;

const float PI = 3.14159265358979323846;

vec3 map_local_to_ecef(vec3 local, vec2 center)
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
    return local.x * center_axis + local.y * east_axis + local.z * north_axis;
}

vec3 ecef_to_render_teme(vec3 ecef, float sidereal_angle)
{
    float c = cos(sidereal_angle);
    float s = sin(sidereal_angle);
    vec3 teme = vec3(
        c * ecef.x - s * ecef.y,
        s * ecef.x + c * ecef.y,
        ecef.z);
    return vec3(-teme.y, teme.z, -teme.x);
}

void main()
{
    bool map_projection = push.camera_pos.w < 0.0;
    vec3 n = normalize(in_normal);
    vec2 uv = vec2(fract(in_uv.x), 1.0 - clamp(in_uv.y, 0.0, 1.0));
    if (map_projection)
    {
        // Quad corner normals collapse to the poles, so recover the sphere per fragment.
        float map_longitude = (in_uv.x - 0.5) * 2.0 * PI;
        float phi = mix(-0.5 * PI, 0.5 * PI, in_uv.y);
        float cp = cos(phi);
        vec3 map_local = vec3(cp * cos(map_longitude), cp * sin(map_longitude), sin(phi));
        vec3 ecef = map_local_to_ecef(map_local, push.camera_orientation.xy);
        n = normalize(ecef_to_render_teme(ecef, push.render_params.z));
        float earth_longitude = atan(ecef.y, ecef.x);
        float earth_latitude = asin(clamp(ecef.z, -1.0, 1.0));
        uv = vec2(
            fract(earth_longitude / (2.0 * PI) + 0.5),
            0.5 - earth_latitude / PI);
    }
    vec3 light = normalize(push.sun_dir_time.xyz);
    vec3 view = map_projection ? n : normalize(push.camera_pos.xyz - in_world);

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
    if (map_projection && push.sun_dir_time.w > 0.5)
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
    out_color = vec4(color, 1.0);
}
