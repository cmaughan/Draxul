#include <metal_stdlib>

using namespace metal;

struct FrameUniforms
{
    float4x4 view;
    float4x4 proj;
    float4 light_dir;
};

struct ObjectUniforms
{
    float4x4 world;
    float4 color;
};

struct VertexIn
{
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float3 color [[attribute(2)]];
};

struct VertexOut
{
    float4 position [[position]];
    float3 color;
};

vertex VertexOut scene_vertex(VertexIn in [[stage_in]],
    constant FrameUniforms& frame [[buffer(1)]],
    constant ObjectUniforms& object [[buffer(2)]])
{
    VertexOut out;
    const float4 world_position = object.world * float4(in.position, 1.0);
    const float3 normal_ws = normalize(float3x3(object.world[0].xyz, object.world[1].xyz, object.world[2].xyz) * in.normal);
    const float3 light_dir = normalize(-frame.light_dir.xyz);
    const float ndotl = max(dot(normal_ws, light_dir), 0.0);
    const float lighting = 0.2 + 0.8 * ndotl;

    out.position = frame.proj * frame.view * world_position;
    out.color = in.color * object.color.rgb * lighting;
    return out;
}

fragment float4 scene_fragment(VertexOut in [[stage_in]])
{
    return float4(in.color, 1.0);
}
