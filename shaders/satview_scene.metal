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

static float hash_wave(float3 p)
{
    return sin(p.x * 2.0f + sin(p.z * 1.8f))
        + 0.7f * sin(p.y * 3.2f + p.x * 1.1f)
        + 0.45f * sin((p.x + p.z) * 3.4f + p.y * 1.5f);
}

fragment float4 satview_earth_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    float3 n = normalize(in.normal);
    float3 light = normalize(frame.sun_dir_time.xyz);
    float3 view = normalize(frame.camera_pos.xyz - in.world);

    float latitude = abs(n.y);
    float continent_noise = hash_wave(n);
    float land_mask = smoothstep(0.20f, 0.60f, continent_noise * 0.25f + 0.48f - latitude * 0.08f);
    float ice_mask = smoothstep(0.74f, 0.92f, latitude);

    float3 ocean = mix(float3(0.015f, 0.075f, 0.17f), float3(0.01f, 0.20f, 0.34f), 1.0f - latitude);
    float3 land = mix(float3(0.13f, 0.32f, 0.15f), float3(0.55f, 0.47f, 0.25f), smoothstep(0.25f, 0.65f, latitude));
    float3 surface = mix(ocean, land, land_mask);
    surface = mix(surface, float3(0.84f, 0.90f, 0.88f), ice_mask);

    float ndl = dot(n, light);
    float day = smoothstep(-0.10f, 0.12f, ndl);
    float diffuse = max(ndl, 0.0f);
    float3 lit = surface * (0.34f + diffuse * 0.90f);
    float3 night = float3(0.006f, 0.013f, 0.030f);

    float city_mask = land_mask
        * smoothstep(-0.40f, 0.02f, n.y)
        * smoothstep(-0.35f, 0.05f, -ndl)
        * smoothstep(0.45f, 0.85f, sin(in.uv.x * 18.0f) * sin(in.uv.y * 13.0f));
    night += float3(1.0f, 0.62f, 0.24f) * city_mask * 0.08f;

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
