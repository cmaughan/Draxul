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
};

struct SatViewMarkerInstance
{
    float4 position0_size;
    float4 position1_selected;
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

vertex SatViewOrbitOut satview_orbit_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    constant SatViewSceneVertex* vertices [[buffer(1)]])
{
    SatViewOrbitOut out;
    SatViewSceneVertex vertex = vertices[vertex_id];
    out.position = frame.view_proj * float4(vertex.position.xyz, 1.0f);
    out.color = vertex.color;
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

    float3 right = normalize(rotate_by_quaternion(float3(1.0f, 0.0f, 0.0f), frame.camera_orientation));
    float3 up = normalize(rotate_by_quaternion(float3(0.0f, 1.0f, 0.0f), frame.camera_orientation));
    float3 axis = segment == 0u ? right
        : segment == 1u ? up
        : segment == 2u ? normalize(right + up)
        : normalize(right - up);

    float3 center = mix(marker.position0_size.xyz, marker.position1_selected.xyz, alpha);
    float3 world = center + axis * marker.position0_size.w * endpoint_sign;

    SatViewOrbitOut out;
    out.position = frame.view_proj * float4(world, 1.0f);
    out.color = marker.color;
    if (segment >= 2u && selected < 0.5f)
        out.color.a = 0.0f;
    return out;
}

fragment float4 satview_orbit_fragment(SatViewOrbitOut in [[stage_in]])
{
    return in.color;
}
