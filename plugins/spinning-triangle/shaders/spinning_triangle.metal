#include <metal_stdlib>
using namespace metal;

struct PushConstants {
    float angle;
    float aspect;
    int mode;
};

struct VertexOut {
    float4 position [[position]];
    float3 color;
};

vertex VertexOut triangle_vertex(uint vertex_id [[vertex_id]],
    constant PushConstants& pc [[buffer(0)]]) {
    const float2 background[6] = {
        {-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0},
        {-1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}
    };
    const float2 triangle[3] = {
        {0.0, -0.62}, {0.54, 0.34}, {-0.54, 0.34}
    };
    const float3 colors[3] = {
        {1.0, 0.18, 0.18}, {0.18, 1.0, 0.32}, {0.2, 0.4, 1.0}
    };
    VertexOut result;
    if (pc.mode == 0) {
        result.position = float4(background[vertex_id], 0.0, 1.0);
        result.color = float3(0.025, 0.035, 0.055);
    } else {
        float2 p = triangle[vertex_id];
        p.x /= max(pc.aspect, 0.001f);
        const float c = cos(pc.angle);
        const float s = sin(pc.angle);
        p = float2(c * p.x - s * p.y, s * p.x + c * p.y);
        result.position = float4(p, 0.0, 1.0);
        result.color = colors[vertex_id];
    }
    return result;
}

fragment float4 triangle_fragment(VertexOut in [[stage_in]]) {
    return float4(in.color, 1.0);
}
