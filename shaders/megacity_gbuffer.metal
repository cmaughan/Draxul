#include <metal_stdlib>

using namespace metal;

struct FrameUniforms
{
    float4x4 view;
    float4x4 proj;
    float4x4 inv_view_proj;
    float4 light_dir;
    float4 point_light_pos;
    float4 label_fade_px;
    float4 render_tuning;
    float4 screen_params;
    float4 ao_params;
};

struct ObjectUniforms
{
    float4x4 world;
    float4 color;
    float4 uv_rect;
    float4 label_metrics;
};

struct VertexIn
{
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float3 color [[attribute(2)]];
    float2 uv [[attribute(3)]];
    float tex_blend [[attribute(4)]];
};

struct VertexOut
{
    float4 position [[position]];
    float3 normal_ws;
    float3 base_color;
};

struct GBufferOut
{
    half4 material [[color(0)]];    // RG = octahedral normal, B = roughness, A = specular
    half4 base_color [[color(1)]];  // RGB = albedo, A = metallic
    half4 ao [[color(2)]];          // R = ambient occlusion, GBA = reserved
};

// Octahedral encoding: unit normal → [0,1]^2
// Reference: "Survey of Efficient Representations for Independent Unit Vectors" (Cigolle et al. 2014)
float2 oct_encode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * select(float2(-1.0), float2(1.0), n.xy >= 0.0);
    return n.xy * 0.5 + 0.5;
}

vertex VertexOut gbuffer_vertex(VertexIn in [[stage_in]],
    constant FrameUniforms& frame [[buffer(1)]],
    constant ObjectUniforms& object [[buffer(2)]])
{
    VertexOut out;
    const float4 world_position = object.world * float4(in.position, 1.0);
    out.position = frame.proj * frame.view * world_position;
    out.normal_ws = normalize(float3x3(object.world[0].xyz, object.world[1].xyz, object.world[2].xyz) * in.normal);
    out.base_color = in.color * object.color.rgb;
    return out;
}

fragment GBufferOut gbuffer_fragment(VertexOut in [[stage_in]])
{
    GBufferOut out;
    const float2 encoded_normal = oct_encode(normalize(in.normal_ws));
    const half roughness = 0.5h;
    const half specular = 0.5h;
    const half metallic = 0.0h;
    const half ao_value = 1.0h;
    out.material = half4(half2(encoded_normal), roughness, specular);
    out.base_color = half4(half3(in.base_color), 1.0h);
    out.ao = half4(ao_value, 0.0h, 0.0h, 1.0h);
    return out;
}
