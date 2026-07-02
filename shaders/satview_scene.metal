#include <metal_stdlib>
using namespace metal;

struct SatViewFrameUniforms
{
    float4x4 view_proj;
    float4 camera_pos;
    float4 camera_orientation;
    float4 sun_dir_time;
    float4 render_params;
};

struct SatViewVertexOut
{
    float4 position [[position]];
    float3 normal;
    float3 world;
    float2 uv;
};

struct SatViewOrbitOut
{
    float4 position [[position]];
    float4 color;
};

struct SatViewSceneVertex
{
    float4 position;
    float4 color;
    float4 paired_position;
};

struct SatViewMarkerInstance
{
    float4 position0_size;
    float4 position1_selected;
    float4 color;
};

constant float kPi = 3.14159265358979323846f;
constant float3 kLunarNorthPoleRender = float3(
    0.39812155f,
    0.91733267f,
    0.00003544f);

static float3 render_teme_to_ecef(float3 render_position, float sidereal_angle)
{
    float3 teme = float3(-render_position.z, -render_position.x, render_position.y);
    float c = cos(sidereal_angle);
    float s = sin(sidereal_angle);
    return float3(
        c * teme.x + s * teme.y,
        -s * teme.x + c * teme.y,
        teme.z);
}

static float3 ecef_to_render_teme(float3 ecef, float sidereal_angle)
{
    float c = cos(sidereal_angle);
    float s = sin(sidereal_angle);
    float3 teme = float3(
        c * ecef.x - s * ecef.y,
        s * ecef.x + c * ecef.y,
        ecef.z);
    return float3(-teme.y, teme.z, -teme.x);
}

static float3 ecef_to_map_local(float3 ecef, float2 center)
{
    float cos_longitude = cos(center.x);
    float sin_longitude = sin(center.x);
    float cos_latitude = cos(center.y);
    float sin_latitude = sin(center.y);
    float3 center_axis = float3(
        cos_latitude * cos_longitude,
        cos_latitude * sin_longitude,
        sin_latitude);
    float3 east_axis = float3(-sin_longitude, cos_longitude, 0.0f);
    float3 north_axis = float3(
        -sin_latitude * cos_longitude,
        -sin_latitude * sin_longitude,
        cos_latitude);
    return float3(
        dot(ecef, center_axis),
        dot(ecef, east_axis),
        dot(ecef, north_axis));
}

static float3 map_local_to_ecef(float3 local, float2 center)
{
    float cos_longitude = cos(center.x);
    float sin_longitude = sin(center.x);
    float cos_latitude = cos(center.y);
    float sin_latitude = sin(center.y);
    float3 center_axis = float3(
        cos_latitude * cos_longitude,
        cos_latitude * sin_longitude,
        sin_latitude);
    float3 east_axis = float3(-sin_longitude, cos_longitude, 0.0f);
    float3 north_axis = float3(
        -sin_latitude * cos_longitude,
        -sin_latitude * sin_longitude,
        cos_latitude);
    return local.x * center_axis + local.y * east_axis + local.z * north_axis;
}

static float3 render_to_lunar_body(
    float3 render_position,
    constant SatViewFrameUniforms& frame)
{
    float3 far_axis = normalize(frame.camera_pos.xyz);
    float3 north_axis = normalize(
        kLunarNorthPoleRender
        - far_axis * dot(kLunarNorthPoleRender, far_axis));
    float3 local_x_axis = normalize(cross(north_axis, far_axis));
    float3 moon_relative = render_position - frame.camera_pos.xyz;
    return float3(
        dot(moon_relative, -far_axis),
        dot(moon_relative, -local_x_axis),
        dot(moon_relative, north_axis));
}

static float2 map_position_from_render_teme(
    float3 render_position,
    constant SatViewFrameUniforms& frame,
    bool earth_fixed)
{
    float3 body_position = frame.camera_orientation.z > 0.5f
        ? render_to_lunar_body(render_position, frame)
        : earth_fixed
            ? render_position
            : render_teme_to_ecef(render_position, frame.render_params.z);
    float3 local = ecef_to_map_local(body_position, frame.camera_orientation.xy);
    float radius = max(length(local), 0.000001f);
    return float2(
        atan2(local.y, local.x) / kPi,
        2.0f * asin(clamp(local.z / radius, -1.0f, 1.0f)) / kPi);
}

static float2 quad_corner(uint corner_index)
{
    switch (corner_index)
    {
    case 0:
        return float2(0.0f, 0.0f);
    case 1:
        return float2(1.0f, 1.0f);
    case 2:
        return float2(1.0f, 0.0f);
    case 3:
        return float2(0.0f, 0.0f);
    case 4:
        return float2(0.0f, 1.0f);
    default:
        return float2(1.0f, 1.0f);
    }
}

vertex SatViewVertexOut satview_earth_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    uint lat_bands = max(1u, uint(frame.render_params.x + 0.5f));
    uint lon_bands = max(1u, uint(frame.render_params.y + 0.5f));
    uint tri_vertex = vertex_id % 6u;
    uint quad = vertex_id / 6u;
    uint lon = quad % lon_bands;
    uint lat = quad / lon_bands;

    float2 corner = quad_corner(tri_vertex);
    float u = (float(lon) + corner.x) / float(lon_bands);
    float v = (float(lat) + corner.y) / float(lat_bands);
    float theta = u * 2.0f * kPi + frame.render_params.z;
    float phi = mix(-0.5f * kPi, 0.5f * kPi, v);
    float cp = cos(phi);
    float3 world = float3(cp * sin(theta), sin(phi), cp * cos(theta));

    SatViewVertexOut out;
    out.position = frame.view_proj * float4(world, 1.0f);
    out.normal = world;
    out.world = world;
    out.uv = float2(u, v);
    return out;
}

fragment float4 satview_earth_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    texture2d<float> earth_day_tex [[texture(0)]],
    texture2d<float> earth_night_tex [[texture(1)]],
    texture2d<float> earth_cloud_tex [[texture(2)]],
    texture2d<float> earth_live_cloud_tex [[texture(3)]],
    sampler earth_sampler [[sampler(0)]])
{
    bool map_projection = frame.camera_pos.w < 0.0f;
    float3 n = normalize(in.normal);
    float2 uv = float2(fract(in.uv.x), 1.0f - clamp(in.uv.y, 0.0f, 1.0f));
    if (map_projection)
    {
        // Quad corner normals collapse to the poles, so recover the sphere per fragment.
        float map_longitude = (in.uv.x - 0.5f) * 2.0f * kPi;
        float phi = mix(-0.5f * kPi, 0.5f * kPi, in.uv.y);
        float cp = cos(phi);
        float3 map_local = float3(
            cp * cos(map_longitude),
            cp * sin(map_longitude),
            sin(phi));
        float3 ecef = map_local_to_ecef(map_local, frame.camera_orientation.xy);
        n = normalize(ecef_to_render_teme(ecef, frame.render_params.z));
        float earth_longitude = atan2(ecef.y, ecef.x);
        float earth_latitude = asin(clamp(ecef.z, -1.0f, 1.0f));
        uv = float2(
            fract(earth_longitude / (2.0f * kPi) + 0.5f),
            0.5f - earth_latitude / kPi);
    }
    float3 light = normalize(frame.sun_dir_time.xyz);
    float3 view = map_projection ? n : normalize(frame.camera_pos.xyz - in.world);

    float3 day_surface = earth_day_tex.sample(earth_sampler, uv).rgb;
    float3 night_surface = earth_night_tex.sample(earth_sampler, uv).rgb;
    float ndl = dot(n, light);
    float day = smoothstep(-0.08f, 0.14f, ndl);
    float diffuse = max(ndl, 0.0f);
    float3 lit = day_surface * (0.22f + diffuse * 1.08f);
    float ocean_hint = smoothstep(0.03f, 0.24f, day_surface.b - max(day_surface.r, day_surface.g));
    float specular = pow(max(dot(reflect(-light, n), view), 0.0f), 48.0f)
        * ocean_hint * smoothstep(0.0f, 0.25f, ndl);
    lit += float3(0.55f, 0.72f, 0.90f) * specular * 0.30f;

    float3 night = night_surface * 1.85f + day_surface * 0.015f;
    float3 color = mix(night, lit, day);
    if (map_projection && frame.sun_dir_time.w > 0.5f)
    {
        float3 bundled_cloud_sample = earth_cloud_tex.sample(earth_sampler, uv).rgb;
        float3 live_cloud_sample = earth_live_cloud_tex.sample(earth_sampler, uv).rgb;
        float3 cloud_sample = mix(
            bundled_cloud_sample,
            live_cloud_sample,
            clamp(frame.sun_dir_time.w - 1.0f, 0.0f, 1.0f));
        float opacity = smoothstep(
            0.20f, 0.78f, dot(cloud_sample, float3(0.299f, 0.587f, 0.114f)));
        float daylight = smoothstep(-0.10f, 0.16f, ndl);
        float3 day_cloud = float3(0.91f, 0.95f, 1.0f) * (0.34f + diffuse * 0.76f);
        float3 night_cloud = float3(0.025f, 0.030f, 0.042f);
        float3 cloud_color = mix(night_cloud, day_cloud, daylight);
        float cloud_alpha = opacity * mix(0.32f, 0.78f, daylight);
        color = mix(color, cloud_color, cloud_alpha);
    }
    return float4(color, 1.0f);
}

vertex SatViewVertexOut satview_moon_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    bool map_projection = frame.camera_pos.w < 0.0f;
    uint lat_bands = max(1u, uint(frame.render_params.x + 0.5f));
    uint lon_bands = max(1u, uint(frame.render_params.y + 0.5f));
    uint tri_vertex = vertex_id % 6u;
    uint quad = vertex_id / 6u;
    uint lon = quad % lon_bands;
    uint lat = quad / lon_bands;

    float2 corner = quad_corner(tri_vertex);
    float u = map_projection
        ? corner.x
        : (float(lon) + corner.x) / float(lon_bands);
    float v = map_projection
        ? corner.y
        : (float(lat) + corner.y) / float(lat_bands);
    float theta = u * 2.0f * kPi;
    float phi = mix(-0.5f * kPi, 0.5f * kPi, v);
    float cp = cos(phi);
    float3 local_normal = float3(cp * sin(theta), sin(phi), cp * cos(theta));

    float3 moon_position = map_projection
        ? frame.camera_pos.xyz
        : frame.camera_orientation.xyz;
    float3 far_axis = normalize(moon_position);
    float3 north_axis = normalize(
        kLunarNorthPoleRender
        - far_axis * dot(kLunarNorthPoleRender, far_axis));
    float3 local_x_axis = normalize(cross(north_axis, far_axis));
    float3 normal = normalize(
        local_x_axis * local_normal.x
        + north_axis * local_normal.y
        + far_axis * local_normal.z);
    float3 world = moon_position + normal * frame.camera_orientation.w;

    SatViewVertexOut out;
    out.position = map_projection
        ? frame.view_proj * float4(u * 2.0f - 1.0f, v * 2.0f - 1.0f, 0.8f, 1.0f)
        : frame.view_proj * float4(world, 1.0f);
    out.normal = normal;
    out.world = world;
    out.uv = float2(u, v);
    return out;
}

fragment float4 satview_moon_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    texture2d<float> moon_tex [[texture(4)]],
    sampler moon_sampler [[sampler(0)]])
{
    bool map_projection = frame.camera_pos.w < 0.0f;
    float2 uv = float2(fract(in.uv.x), 1.0f - clamp(in.uv.y, 0.0f, 1.0f));
    float3 normal = normalize(in.normal);
    float3 moon_position = map_projection
        ? frame.camera_pos.xyz
        : frame.camera_orientation.xyz;
    if (map_projection)
    {
        float map_longitude = (in.uv.x - 0.5f) * 2.0f * kPi;
        float map_latitude = mix(-0.5f * kPi, 0.5f * kPi, in.uv.y);
        float cp = cos(map_latitude);
        float3 map_local = float3(
            cp * cos(map_longitude),
            cp * sin(map_longitude),
            sin(map_latitude));
        float3 body = map_local_to_ecef(map_local, frame.camera_orientation.xy);
        float3 far_axis = normalize(moon_position);
        float3 north_axis = normalize(
            kLunarNorthPoleRender
            - far_axis * dot(kLunarNorthPoleRender, far_axis));
        float3 local_x_axis = normalize(cross(north_axis, far_axis));
        normal = normalize(
            -far_axis * body.x
            - local_x_axis * body.y
            + north_axis * body.z);
        float longitude = atan2(body.y, body.x);
        float latitude = asin(clamp(body.z, -1.0f, 1.0f));
        uv = float2(
            fract(longitude / (2.0f * kPi) + 0.5f),
            0.5f - latitude / kPi);
    }
    float3 surface = moon_tex.sample(moon_sampler, uv).rgb;
    float3 sunlight_direction = normalize(frame.sun_dir_time.xyz);
    float3 earth_direction = -normalize(moon_position);
    float diffuse = max(dot(normal, sunlight_direction), 0.0f);
    float earth_phase = 0.5f * (1.0f - dot(earth_direction, sunlight_direction));
    float earthshine = max(dot(normal, earth_direction), 0.0f) * earth_phase * 0.055f;
    float illumination = 0.006f + diffuse * 1.12f + earthshine;
    return float4(surface * illumination, 1.0f);
}

constant float kCloudRadius = 1.0015f;

vertex SatViewVertexOut satview_cloud_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    bool map_projection = frame.camera_pos.w < 0.0f;
    uint lat_bands = max(1u, uint(frame.render_params.x + 0.5f));
    uint lon_bands = max(1u, uint(frame.render_params.y + 0.5f));
    uint tri_vertex = vertex_id % 6u;
    uint quad = vertex_id / 6u;
    uint lon = quad % lon_bands;
    uint lat = quad / lon_bands;

    float2 corner = quad_corner(tri_vertex);
    float u = map_projection
        ? corner.x
        : (float(lon) + corner.x) / float(lon_bands);
    float v = map_projection
        ? corner.y
        : (float(lat) + corner.y) / float(lat_bands);
    float theta = u * 2.0f * kPi + frame.render_params.z;
    float phi = mix(-0.5f * kPi, 0.5f * kPi, v);
    float cp = cos(phi);
    float3 normal = float3(cp * sin(theta), sin(phi), cp * cos(theta));
    float3 world = normal * kCloudRadius;

    SatViewVertexOut out;
    out.position = map_projection
        ? frame.view_proj * float4(u * 2.0f - 1.0f, v * 2.0f - 1.0f, 0.8f, 1.0f)
        : frame.view_proj * float4(world, 1.0f);
    out.normal = normal;
    out.world = world;
    out.uv = float2(u, v);
    return out;
}

static float2 cloud_ray_sphere(float3 origin, float3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0f)
        return float2(-1.0f);
    float root = sqrt(discriminant);
    return float2(-b - root, -b + root);
}

fragment float4 satview_cloud_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    texture2d<float> earth_cloud_tex [[texture(2)]],
    texture2d<float> earth_live_cloud_tex [[texture(3)]],
    sampler earth_sampler [[sampler(0)]])
{
    float cloud_mode = frame.sun_dir_time.w;
    if (cloud_mode < 0.5f)
        discard_fragment();

    float3 ray_origin = frame.camera_pos.xyz;
    float3 ray_direction = normalize(in.world - ray_origin);
    float2 shell_hit = cloud_ray_sphere(ray_origin, ray_direction, kCloudRadius);
    float fragment_distance = length(in.world - ray_origin);
    if (fragment_distance > 0.5f * (shell_hit.x + shell_hit.y))
        discard_fragment();

    float2 uv = float2(fract(in.uv.x), 1.0f - clamp(in.uv.y, 0.0f, 1.0f));
    float3 bundled_cloud_sample = earth_cloud_tex.sample(earth_sampler, uv).rgb;
    float3 live_cloud_sample = earth_live_cloud_tex.sample(earth_sampler, uv).rgb;
    float3 cloud_sample = mix(
        bundled_cloud_sample, live_cloud_sample, clamp(cloud_mode - 1.0f, 0.0f, 1.0f));
    float opacity = smoothstep(
        0.20f, 0.78f, dot(cloud_sample, float3(0.299f, 0.587f, 0.114f)));
    if (opacity < 0.002f)
        discard_fragment();

    float3 normal = normalize(in.normal);
    float3 light = normalize(frame.sun_dir_time.xyz);
    float ndl = dot(normal, light);
    float daylight = smoothstep(-0.10f, 0.16f, ndl);
    float diffuse = max(ndl, 0.0f);
    float3 day_color = float3(0.91f, 0.95f, 1.0f) * (0.34f + diffuse * 0.76f);
    float3 night_color = float3(0.025f, 0.030f, 0.042f);
    float3 cloud_color = mix(night_color, day_color, daylight);
    float alpha = opacity * mix(0.32f, 0.78f, daylight);
    return float4(cloud_color * alpha, alpha);
}

constant float kAtmosphereRadius = 1.02f;
constant float kRayleighScaleHeight = 0.00125f;
constant float kMieScaleHeight = 0.00019f;
constant float3 kRayleighBeta = float3(36.0f, 84.0f, 205.0f);
constant float3 kMieBeta = float3(22.0f, 20.0f, 18.0f);
constant float kMieG = 0.76f;
constant int kAtmosphereViewSteps = 12;
constant int kAtmosphereLightSteps = 6;

vertex SatViewVertexOut satview_atmosphere_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    uint lat_bands = max(1u, uint(frame.render_params.x + 0.5f));
    uint lon_bands = max(1u, uint(frame.render_params.y + 0.5f));
    uint tri_vertex = vertex_id % 6u;
    uint quad = vertex_id / 6u;
    uint lon = quad % lon_bands;
    uint lat = quad / lon_bands;

    float2 corner = quad_corner(tri_vertex);
    float u = (float(lon) + corner.x) / float(lon_bands);
    float v = (float(lat) + corner.y) / float(lat_bands);
    float theta = u * 2.0f * kPi + frame.render_params.z;
    float phi = mix(-0.5f * kPi, 0.5f * kPi, v);
    float cp = cos(phi);
    float3 world = kAtmosphereRadius * float3(cp * sin(theta), sin(phi), cp * cos(theta));

    SatViewVertexOut out;
    out.position = frame.view_proj * float4(world, 1.0f);
    out.normal = normalize(world);
    out.world = world;
    out.uv = float2(u, v);
    return out;
}

static float2 atmosphere_ray_sphere(float3 origin, float3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0f)
        return float2(-1.0f);
    float root = sqrt(discriminant);
    return float2(-b - root, -b + root);
}

static float2 atmosphere_density(float3 position)
{
    float altitude = max(length(position) - 1.0f, 0.0f);
    return float2(
        exp(-altitude / kRayleighScaleHeight),
        exp(-altitude / kMieScaleHeight));
}

static float2 atmosphere_optical_depth_to_sun(float3 origin, float3 sun_direction)
{
    float2 planet_hit = atmosphere_ray_sphere(origin, sun_direction, 1.0f);
    if (planet_hit.x > 0.00001f)
        return float2(-1.0f);

    float2 atmosphere_hit = atmosphere_ray_sphere(origin, sun_direction, kAtmosphereRadius);
    if (atmosphere_hit.y <= 0.0f)
        return float2(-1.0f);

    float2 optical_depth = float2(0.0f);
    for (int step_index = 0; step_index < kAtmosphereLightSteps; ++step_index)
    {
        float u0 = float(step_index) / float(kAtmosphereLightSteps);
        float u1 = float(step_index + 1) / float(kAtmosphereLightSteps);
        float distance0 = atmosphere_hit.y * u0 * u0;
        float distance1 = atmosphere_hit.y * u1 * u1;
        float step_length = distance1 - distance0;
        float distance = 0.5f * (distance0 + distance1);
        optical_depth += atmosphere_density(origin + sun_direction * distance) * step_length;
    }
    return optical_depth;
}

fragment float4 satview_atmosphere_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    float3 ray_origin = frame.camera_pos.xyz;
    float3 ray_direction = normalize(in.world - ray_origin);
    float3 sun_direction = normalize(frame.sun_dir_time.xyz);

    float2 atmosphere_hit = atmosphere_ray_sphere(ray_origin, ray_direction, kAtmosphereRadius);
    if (atmosphere_hit.y <= 0.0f)
        discard_fragment();

    float ray_start = max(atmosphere_hit.x, 0.0f);
    float ray_end = atmosphere_hit.y;
    float fragment_distance = length(in.world - ray_origin);
    if (fragment_distance > 0.5f * (ray_start + ray_end))
        discard_fragment();

    float2 planet_hit = atmosphere_ray_sphere(ray_origin, ray_direction, 1.0f);
    bool hits_planet = planet_hit.x > ray_start && planet_hit.x < ray_end;
    if (hits_planet)
        ray_end = planet_hit.x;
    if (ray_end <= ray_start)
        discard_fragment();

    float segment_length = ray_end - ray_start;
    float2 view_optical_depth = float2(0.0f);
    float3 rayleigh_sum = float3(0.0f);
    float3 mie_sum = float3(0.0f);
    for (int step_index = 0; step_index < kAtmosphereViewSteps; ++step_index)
    {
        float u0 = float(step_index) / float(kAtmosphereViewSteps);
        float u1 = float(step_index + 1) / float(kAtmosphereViewSteps);
        float distance0 = segment_length * (hits_planet
                ? 1.0f - (1.0f - u0) * (1.0f - u0)
                : u0);
        float distance1 = segment_length * (hits_planet
                ? 1.0f - (1.0f - u1) * (1.0f - u1)
                : u1);
        float step_length = distance1 - distance0;
        float distance = ray_start + 0.5f * (distance0 + distance1);
        float3 sample_position = ray_origin + ray_direction * distance;
        float2 density = atmosphere_density(sample_position);
        float2 light_optical_depth = atmosphere_optical_depth_to_sun(sample_position, sun_direction);
        if (light_optical_depth.x >= 0.0f)
        {
            float2 total_optical_depth = view_optical_depth + light_optical_depth;
            float3 transmittance = exp(-(
                kRayleighBeta * total_optical_depth.x
                + kMieBeta * total_optical_depth.y));
            rayleigh_sum += density.x * transmittance * step_length;
            mie_sum += density.y * transmittance * step_length;
        }
        view_optical_depth += density * step_length;
    }

    float cosine = dot(-ray_direction, sun_direction);
    float rayleigh_phase = 3.0f * (1.0f + cosine * cosine) / (16.0f * kPi);
    float mie_denominator = pow(1.0f + kMieG * kMieG - 2.0f * kMieG * cosine, 1.5f);
    float mie_phase = (1.0f - kMieG * kMieG) / (4.0f * kPi * mie_denominator);
    float3 scattering = (
        rayleigh_sum * kRayleighBeta * rayleigh_phase
        + mie_sum * kMieBeta * mie_phase)
        * 6.0f;
    float extinction = dot(
        kRayleighBeta * view_optical_depth.x + kMieBeta * view_optical_depth.y,
        float3(0.2126f, 0.7152f, 0.0722f));
    float alpha = clamp(1.0f - exp(-extinction), 0.0f, 0.92f);
    scattering = min(scattering, float3(alpha));
    return float4(scattering, alpha);
}

vertex SatViewOrbitOut satview_orbit_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    constant SatViewSceneVertex* vertices [[buffer(1)]])
{
    SatViewOrbitOut out;
    SatViewSceneVertex scene_vertex = vertices[vertex_id];
    if (frame.camera_pos.w < 0.0f)
    {
        bool earth_fixed = abs(scene_vertex.position.w) > 1.5f;
        float2 projected = map_position_from_render_teme(scene_vertex.position.xyz, frame, earth_fixed);
        float2 paired = map_position_from_render_teme(scene_vertex.paired_position.xyz, frame, earth_fixed);
        if (scene_vertex.position.w > 0.0f)
        {
            float delta = projected.x - paired.x;
            if (delta > 1.0f)
                projected.x -= 2.0f;
            else if (delta < -1.0f)
                projected.x += 2.0f;
        }
        projected.x += frame.camera_orientation.w;
        out.position = frame.view_proj * float4(projected, 0.4f, 1.0f);
    }
    else
    {
        out.position = frame.view_proj * float4(scene_vertex.position.xyz, 1.0f);
    }
    out.color = scene_vertex.color;
    return out;
}

static float3 rotate_by_quaternion(float3 value, float4 quaternion)
{
    float3 twice_cross = 2.0f * cross(quaternion.xyz, value);
    return value + quaternion.w * twice_cross + cross(quaternion.xyz, twice_cross);
}

vertex SatViewOrbitOut satview_marker_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    constant SatViewMarkerInstance* markers [[buffer(1)]])
{
    SatViewMarkerInstance marker = markers[instance_id];
    uint segment = vertex_id / 2u;
    uint endpoint = vertex_id & 1u;
    float endpoint_sign = endpoint == 0u ? -1.0f : 1.0f;
    float selected = marker.position1_selected.w;
    float alpha = clamp(frame.render_params.w, 0.0f, 1.0f);

    float3 center = mix(marker.position0_size.xyz, marker.position1_selected.xyz, alpha);
    SatViewOrbitOut out;
    out.color = marker.color;
    if (segment >= 2u && selected < 0.5f)
        out.color.a = 0.0f;
    if (frame.camera_pos.w < 0.0f)
    {
        float2 map_center = map_position_from_render_teme(center, frame, false);
        float x_scale = abs(frame.camera_pos.w);
        float2 map_axis = segment == 0u ? float2(x_scale, 0.0f)
            : segment == 1u ? float2(0.0f, 1.0f)
            : segment == 2u ? float2(x_scale, 1.0f) * 0.70710678f
            : float2(x_scale, -1.0f) * 0.70710678f;
        float2 map_position = map_center
            + map_axis * marker.position0_size.w * 0.75f * endpoint_sign;
        out.position = frame.view_proj * float4(map_position, 0.2f, 1.0f);
    }
    else
    {
        float3 right = normalize(rotate_by_quaternion(float3(1.0f, 0.0f, 0.0f), frame.camera_orientation));
        float3 up = normalize(rotate_by_quaternion(float3(0.0f, 1.0f, 0.0f), frame.camera_orientation));
        float3 axis = segment == 0u ? right
            : segment == 1u ? up
            : segment == 2u ? normalize(right + up)
            : normalize(right - up);
        float3 world = center + axis * marker.position0_size.w * endpoint_sign;
        out.position = frame.view_proj * float4(world, 1.0f);
    }
    return out;
}

fragment float4 satview_orbit_fragment(SatViewOrbitOut in [[stage_in]])
{
    return in.color;
}
