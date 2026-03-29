#include <metal_stdlib>

using namespace metal;

struct FrameUniforms
{
    float4x4 view;
    float4x4 proj;
    float4x4 inv_view_proj;
    float4 camera_pos;
    float4 light_dir;
    float4 point_light_pos;
    float4 label_fade_px;
    float4 render_tuning;
    float4 perf_tuning;
    float4 screen_params;
    float4 ao_params;
    float4 debug_view;
    float4 world_debug_bounds;
    float4x4 shadow_view_proj[3];
    float4x4 shadow_texture_matrix[3];
    float4 shadow_split_depths;
    float4 shadow_params;
    float4x4 point_shadow_view_proj[6];
    float4x4 point_shadow_texture_matrix[6];
    float4 point_shadow_params;
};

struct ObjectUniforms
{
    float4x4 world;
    float4 color;
    uint4 material_data;
    float4 uv_rect;
    float4 label_metrics;
};

struct MaterialInstance
{
    float4 scalar_params;
    uint4 texture_indices;
    uint4 metadata;
};

struct MaterialUniforms
{
    MaterialInstance materials[64];
};

struct VertexIn
{
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float3 color [[attribute(2)]];
    float2 uv [[attribute(3)]];
    float tex_blend [[attribute(4)]];
    float4 tangent [[attribute(5)]];
    float layer_id [[attribute(6)]];
};

struct VertexOut
{
    float4 position [[position]];
    float3 normal_ws;
    uint material_index;
    float2 material_uv;
};

struct GBufferOut
{
    half4 normal [[color(0)]]; // RG = octahedral normal, BA reserved
};

struct ShadowVertexOut
{
    float4 position [[position]];
};

struct PointShadowVertexOut
{
    float4 position [[position]];
    float3 world_position;
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
    out.material_index = object.material_data.x;
    out.material_uv = in.uv * object.uv_rect.zw;
    return out;
}

vertex ShadowVertexOut shadow_vertex(VertexIn in [[stage_in]],
    constant FrameUniforms& frame [[buffer(1)]],
    constant ObjectUniforms& object [[buffer(2)]])
{
    ShadowVertexOut out;
    const float4 world_position = object.world * float4(in.position, 1.0f);
    const uint cascade_index = min(object.material_data.y, 2u);
    out.position = frame.shadow_view_proj[cascade_index] * world_position;
    return out;
}

vertex PointShadowVertexOut point_shadow_vertex(VertexIn in [[stage_in]],
    constant FrameUniforms& frame [[buffer(1)]],
    constant ObjectUniforms& object [[buffer(2)]])
{
    PointShadowVertexOut out;
    const float4 world_position = object.world * float4(in.position, 1.0f);
    const uint face_index = min(object.material_data.z, 5u);
    out.position = frame.point_shadow_view_proj[face_index] * world_position;
    out.world_position = world_position.xyz;
    return out;
}

fragment float point_shadow_fragment(
    PointShadowVertexOut in [[stage_in]],
    constant FrameUniforms& frame [[buffer(1)]])
{
    const float radius = max(frame.point_light_pos.w, 1.0f);
    const float distance_to_light = length(in.world_position - frame.point_light_pos.xyz);
    return clamp(distance_to_light / radius, 0.0f, 1.0f);
}

fragment GBufferOut gbuffer_fragment(
    VertexOut in [[stage_in]],
    constant MaterialUniforms& materialTable [[buffer(3)]],
    array<texture2d<float>, 25> materialTextures [[texture(2)]],
    sampler materialSampler [[sampler(2)]])
{
    GBufferOut out;
    constexpr uint kShadingLeafCutoutPbr = 3u;
    constexpr float kLeafAlphaCutoff = 0.35f;
    const MaterialInstance material = materialTable.materials[min(in.material_index, 63u)];
    if (material.metadata.x == kShadingLeafCutoutPbr)
    {
        const float2 material_uv = in.material_uv * material.scalar_params.x;
        const float opacity = materialTextures[material.texture_indices.w].sample(materialSampler, material_uv).r;
        if (opacity < kLeafAlphaCutoff)
            discard_fragment();
    }

    float3 normal_ws = normalize(in.normal_ws);
    out.normal = half4(half2(oct_encode(normalize(normal_ws))), 0.0h, 1.0h);
    return out;
}
