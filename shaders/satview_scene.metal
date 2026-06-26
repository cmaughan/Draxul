#include <metal_stdlib>
using namespace metal;

struct SatViewFrameUniforms
{
    float4x4 view_proj;
    float4 camera_pos;
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

constant float kPi = 3.14159265358979323846f;

static float2 quad_corner(uint vertex)
{
    switch (vertex)
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
    sampler earth_sampler [[sampler(0)]])
{
    float3 n = normalize(in.normal);
    float3 light = normalize(frame.sun_dir_time.xyz);
    float3 view = normalize(frame.camera_pos.xyz - in.world);
    float2 uv = float2(fract(in.uv.x), 1.0f - clamp(in.uv.y, 0.0f, 1.0f));

    float3 day_surface = earth_day_tex.sample(earth_sampler, uv).rgb;
    float3 night_surface = earth_night_tex.sample(earth_sampler, uv).rgb;
    float3 cloud_sample = earth_cloud_tex.sample(
        earth_sampler, uv + float2(frame.sun_dir_time.w * 0.0000007f, 0.0f)).rgb;
    float cloud_alpha = smoothstep(0.20f, 0.78f, dot(cloud_sample, float3(0.299f, 0.587f, 0.114f)));

    float ndl = dot(n, light);
    float day = smoothstep(-0.08f, 0.14f, ndl);
    float diffuse = max(ndl, 0.0f);
    float3 lit = day_surface * (0.22f + diffuse * 1.08f);
    float ocean_hint = smoothstep(0.03f, 0.24f, day_surface.b - max(day_surface.r, day_surface.g));
    float specular = pow(max(dot(reflect(-light, n), view), 0.0f), 48.0f)
        * ocean_hint * smoothstep(0.0f, 0.25f, ndl);
    lit += float3(0.55f, 0.72f, 0.90f) * specular * 0.30f;

    float3 night = night_surface * 1.85f + day_surface * 0.015f;
    float3 cloud_day = float3(0.92f, 0.96f, 1.0f) * (0.28f + diffuse * 0.92f);
    float3 cloud_night = float3(0.045f, 0.050f, 0.065f);
    lit = mix(lit, cloud_day, cloud_alpha * 0.58f);
    night = mix(night, max(night, cloud_night), cloud_alpha * 0.35f);

    float rim = pow(1.0f - max(dot(view, n), 0.0f), 3.0f);
    float3 atmosphere = float3(0.12f, 0.36f, 0.66f) * rim * (0.45f + day * 0.65f);
    float3 color = mix(night, lit, day) + atmosphere;
    return float4(color, 1.0f);
}

static float3 orbit_palette(float t)
{
    return 0.58f + 0.42f * cos(6.28318f * (float3(0.05f, 0.38f, 0.67f) + t));
}

vertex SatViewOrbitOut satview_orbit_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    uint segment_count = 192u;
    uint pair = vertex_id / 2u;
    uint endpoint = vertex_id - pair * 2u;
    uint segment = pair % segment_count;
    uint track = pair / segment_count;

    float track_f = float(track);
    float family = fmod(track_f, 3.0f);
    float radius = family < 0.5f ? 1.28f : (family < 1.5f ? 1.58f : 2.05f);
    radius += 0.035f * sin(track_f * 2.37f);

    float inclination = mix(0.10f, 1.45f, fract(track_f * 0.31831f));
    if (family > 1.5f)
        inclination *= 0.12f;
    float raan = track_f * 0.61803398875f * 2.0f * kPi + frame.sun_dir_time.w * 0.000006f;
    float anomaly = (float(segment + endpoint) / float(segment_count)) * 2.0f * kPi;
    anomaly += frame.sun_dir_time.w * (family < 0.5f ? 0.00023f : (family < 1.5f ? 0.00009f : 0.00002f));

    float3 local = float3(cos(anomaly) * radius, 0.0f, sin(anomaly) * radius);
    float ci = cos(inclination);
    float si = sin(inclination);
    float3 inclined = float3(local.x, -local.z * si, local.z * ci);
    float cr = cos(raan);
    float sr = sin(raan);
    float3 world = float3(
        inclined.x * cr + inclined.z * sr,
        inclined.y,
        -inclined.x * sr + inclined.z * cr);

    float3 color = mix(orbit_palette(fract(track_f * 0.173f)), float3(1.0f), 0.20f);
    float3 view = normalize(frame.camera_pos.xyz - world);
    float frontness = max(dot(normalize(world), view), 0.0f);
    float face_fade = mix(1.0f, 0.68f, smoothstep(0.05f, 0.70f, frontness));
    float alpha = (family < 0.5f ? 0.78f : (family < 1.5f ? 0.64f : 0.86f)) * face_fade;

    SatViewOrbitOut out;
    out.position = frame.view_proj * float4(world, 1.0f);
    out.color = float4(color, alpha);
    return out;
}

fragment float4 satview_orbit_fragment(SatViewOrbitOut in [[stage_in]])
{
    return in.color;
}
