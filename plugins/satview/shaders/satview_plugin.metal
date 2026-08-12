#include <metal_stdlib>
using namespace metal;

struct SceneState { float time; float aspect; int paused; };
struct VertexOut { float4 position [[position]]; float2 uv; };

vertex VertexOut satview_vertex(uint vertex_id [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VertexOut out;
    out.position = float4(positions[vertex_id], 0.0, 1.0);
    out.uv = positions[vertex_id] * 0.5 + 0.5;
    return out;
}

float hash21(float2 p)
{
    p = fract(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

fragment float4 satview_fragment(VertexOut in [[stage_in]],
    constant SceneState& scene [[buffer(0)]])
{
    float2 p = in.uv * 2.0 - 1.0;
    p.x *= scene.aspect;
    float3 color = float3(0.008, 0.014, 0.035);
    float2 star_cell = floor((p + float2(scene.time * 0.002, 0.0)) * 95.0);
    color += step(0.993, hash21(star_cell)) * float3(0.7, 0.82, 1.0);
    float radius = length(p);
    float earth = 1.0 - smoothstep(0.405, 0.415, radius);
    float2 sphere = p / 0.415;
    float3 normal = float3(sphere, sqrt(max(0.0, 1.0 - dot(sphere, sphere))));
    float lighting = 0.16 + 0.84 * max(0.0, dot(normal, normalize(float3(0.75, -0.3, 0.8))));
    float bands = 0.5 + 0.5 * sin(18.0 * p.y + 2.0 * sin(9.0 * p.x));
    float3 ocean = mix(float3(0.015, 0.08, 0.24), float3(0.03, 0.28, 0.52), lighting);
    float3 land = float3(0.08, 0.34, 0.14) * (0.55 + lighting);
    color = mix(color, mix(ocean, land, smoothstep(0.70, 0.86, bands)), earth);
    for (int i = 0; i < 4; ++i) {
        float orbit_radius = 0.56 + float(i) * 0.105;
        float flatten = 1.35 + float(i) * 0.09;
        float ring = 1.0 - smoothstep(0.002, 0.008,
            abs(length(float2(p.x, p.y * flatten)) - orbit_radius));
        color += ring * float3(0.06, 0.18, 0.32);
        float angle = scene.time * (0.42 + float(i) * 0.11) + float(i) * 1.7;
        float2 satellite = float2(cos(angle) * orbit_radius,
            sin(angle) * orbit_radius / flatten);
        float marker = 1.0 - smoothstep(0.010, 0.022, length(p - satellite));
        color += marker * mix(float3(0.3, 0.7, 1.0),
            float3(1.0, 0.5, 0.15), float(i) / 3.0);
    }
    float atmosphere = (1.0 - smoothstep(0.414, 0.455, radius))
        * smoothstep(0.405, 0.423, radius);
    color += atmosphere * float3(0.08, 0.35, 0.8);
    return float4(color, 1.0);
}
