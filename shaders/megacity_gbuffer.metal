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
    float4 screen_params;
    float4 ao_params;
    float4 debug_view;
    float4 world_debug_bounds;
};

struct ObjectUniforms
{
    float4x4 world;
    float4 color;
    float4 material_info;
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
    float3 world_position;
    float4 material_info [[flat]];
};

struct GBufferOut
{
    half4 material [[color(0)]];    // RG = octahedral normal, B = roughness, A = material AO
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
    out.world_position = world_position.xyz;
    out.material_info = object.material_info;
    return out;
}

fragment GBufferOut gbuffer_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> roadAlbedoTexture [[texture(0)]],
    texture2d<float> roadNormalTexture [[texture(1)]],
    texture2d<float> roadRoughnessTexture [[texture(2)]],
    texture2d<float> roadAoTexture [[texture(3)]],
    sampler materialSampler [[sampler(0)]])
{
    GBufferOut out;
    float3 normal_ws = normalize(in.normal_ws);
    float3 albedo = in.base_color;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float material_ao = 1.0f;

    if (in.material_info.x > 0.5f)
    {
        const float2 road_uv = in.world_position.xz * in.material_info.y;
        float3 tangent_normal = roadNormalTexture.sample(materialSampler, road_uv).xyz * 2.0f - 1.0f;
        tangent_normal.xy *= max(in.material_info.z, 0.0f);
        tangent_normal = normalize(tangent_normal);

        const float3x3 tbn = float3x3(float3(1.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), normal_ws);
        normal_ws = normalize(tbn * tangent_normal);
        albedo = roadAlbedoTexture.sample(materialSampler, road_uv).rgb * in.base_color;
        roughness = clamp(roadRoughnessTexture.sample(materialSampler, road_uv).r, 0.04f, 1.0f);
        material_ao = mix(1.0f, roadAoTexture.sample(materialSampler, road_uv).r, clamp(in.material_info.w, 0.0f, 1.0f));
    }

    out.material = half4(half2(oct_encode(normalize(normal_ws))), half(roughness), half(material_ao));
    out.base_color = half4(half3(albedo), half(metallic));
    out.ao = half4(1.0h, 0.0h, 0.0h, 1.0h);
    return out;
}
