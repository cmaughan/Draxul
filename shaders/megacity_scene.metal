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
    float2 atlas_uv;
    float tex_blend;
    float2 label_ink_pixel_size;
    float4 material_info;
};

vertex VertexOut scene_vertex(VertexIn in [[stage_in]],
    constant FrameUniforms& frame [[buffer(1)]],
    constant ObjectUniforms& object [[buffer(2)]])
{
    VertexOut out;
    const float4 world_position = object.world * float4(in.position, 1.0);
    out.position = frame.proj * frame.view * world_position;
    out.normal_ws = normalize(float3x3(object.world[0].xyz, object.world[1].xyz, object.world[2].xyz) * in.normal);
    out.base_color = in.color * object.color.rgb;
    out.world_position = world_position.xyz;
    out.atlas_uv = mix(object.uv_rect.xy, object.uv_rect.zw, in.uv);
    out.tex_blend = in.tex_blend;
    out.label_ink_pixel_size = object.label_metrics.xy;
    out.material_info = object.material_info;
    return out;
}

float distribution_ggx(float3 n, float3 h, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float ndoth = max(dot(n, h), 0.0f);
    const float ndoth2 = ndoth * ndoth;
    const float denom = ndoth2 * (a2 - 1.0f) + 1.0f;
    return a2 / max(3.14159265359f * denom * denom, 1e-4f);
}

float geometry_schlick_ggx(float ndotv, float roughness)
{
    const float r = roughness + 1.0f;
    const float k = (r * r) * 0.125f;
    return ndotv / max(ndotv * (1.0f - k) + k, 1e-4f);
}

float geometry_smith(float3 n, float3 v, float3 l, float roughness)
{
    return geometry_schlick_ggx(max(dot(n, v), 0.0f), roughness)
        * geometry_schlick_ggx(max(dot(n, l), 0.0f), roughness);
}

float3 fresnel_schlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

constant int kMaterialAsphaltRoad = 1;
constant int kMaterialWoodBuilding = 2;

int material_id(float4 material_info)
{
    return int(floor(material_info.x + 0.5f));
}

void dominant_axis_mapping(float3 world_position, float3 normal_ws, float uv_scale,
    thread float2& uv, thread float3x3& tbn)
{
    const float3 abs_n = abs(normal_ws);
    if (abs_n.y >= abs_n.x && abs_n.y >= abs_n.z)
    {
        const float sign_y = normal_ws.y >= 0.0f ? 1.0f : -1.0f;
        uv = float2(world_position.x, -sign_y * world_position.z) * uv_scale;
        tbn = float3x3(
            float3(1.0f, 0.0f, 0.0f),
            float3(0.0f, 0.0f, -sign_y),
            float3(0.0f, sign_y, 0.0f));
        return;
    }

    if (abs_n.x >= abs_n.z)
    {
        const float sign_x = normal_ws.x >= 0.0f ? 1.0f : -1.0f;
        uv = float2(-sign_x * world_position.z, world_position.y) * uv_scale;
        tbn = float3x3(
            float3(0.0f, 0.0f, -sign_x),
            float3(0.0f, 1.0f, 0.0f),
            float3(sign_x, 0.0f, 0.0f));
        return;
    }

    const float sign_z = normal_ws.z >= 0.0f ? 1.0f : -1.0f;
    uv = float2(sign_z * world_position.x, world_position.y) * uv_scale;
    tbn = float3x3(
        float3(sign_z, 0.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, 0.0f, sign_z));
}

fragment float4 scene_fragment(
    VertexOut in [[stage_in]],
    constant FrameUniforms& frame [[buffer(1)]],
    texture2d<float> signAtlas [[texture(0)]],
    texture2d<float> aoTexture [[texture(1)]],
    texture2d<float> roadAlbedoTexture [[texture(2)]],
    texture2d<float> roadNormalTexture [[texture(3)]],
    texture2d<float> roadRoughnessTexture [[texture(4)]],
    texture2d<float> roadAoTexture [[texture(5)]],
    texture2d<float> woodAlbedoTexture [[texture(6)]],
    texture2d<float> woodNormalTexture [[texture(7)]],
    texture2d<float> woodRoughnessTexture [[texture(8)]],
    texture2d<float> woodAoTexture [[texture(9)]],
    sampler signSampler [[sampler(0)]],
    sampler aoSampler [[sampler(1)]],
    sampler materialSampler [[sampler(2)]])
{
    float3 normal_ws = normalize(in.normal_ws);
    float3 albedo = in.base_color;
    float roughness = 0.65f;
    const float metallic = 0.0f;
    float material_ao = 1.0f;

    const int id = material_id(in.material_info);
    if (id == kMaterialAsphaltRoad)
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
    else if (id == kMaterialWoodBuilding)
    {
        float2 wood_uv = float2(0.0f);
        float3x3 tbn;
        dominant_axis_mapping(in.world_position, normal_ws, in.material_info.y, wood_uv, tbn);

        float3 tangent_normal = woodNormalTexture.sample(materialSampler, wood_uv).xyz * 2.0f - 1.0f;
        tangent_normal.xy *= max(in.material_info.z, 0.0f);
        tangent_normal = normalize(tangent_normal);

        normal_ws = normalize(tbn * tangent_normal);
        albedo = in.base_color;
        roughness = clamp(woodRoughnessTexture.sample(materialSampler, wood_uv).r, 0.04f, 1.0f);
        material_ao = mix(1.0f, woodAoTexture.sample(materialSampler, wood_uv).r, clamp(in.material_info.w, 0.0f, 1.0f));
    }

    const float2 screen_uv = clamp(
        (in.position.xy - frame.screen_params.xy) * frame.screen_params.zw,
        float2(0.0f),
        float2(1.0f));
    const float3 ao_debug = aoTexture.sample(aoSampler, screen_uv).rgb;
    const float ao = clamp(ao_debug.r, 0.0f, 1.0f);
    if (frame.debug_view.x > 0.5f)
        return float4(ao_debug, 1.0f);

    const float ambient = max(frame.render_tuning.z, 0.0f);
    const float3 view_dir = normalize(frame.camera_pos.xyz - in.world_position);
    const float3 f0 = mix(float3(0.04f), albedo, metallic);
    const float hemi_factor = normal_ws.y * 0.5f + 0.5f;
    const float3 hemi = mix(float3(0.84f, 0.82f, 0.78f), float3(1.04f, 1.03f, 1.01f), hemi_factor);
    const float combined_ao = material_ao * ao;

    float3 direct_lighting = float3(0.0f);

    const float3 light_dir = normalize(-frame.light_dir.xyz);
    const float ndotl = max(dot(normal_ws, light_dir), 0.0f);
    if (ndotl > 0.0f)
    {
        const float3 half_vec = normalize(view_dir + light_dir);
        const float3 fresnel = fresnel_schlick(max(dot(half_vec, view_dir), 0.0f), f0);
        const float ndf = distribution_ggx(normal_ws, half_vec, roughness);
        const float geometry = geometry_smith(normal_ws, view_dir, light_dir, roughness);
        const float3 specular = (ndf * geometry * fresnel)
            / max(4.0f * max(dot(normal_ws, view_dir), 0.0f) * ndotl, 1e-4f);
        const float3 kd = (1.0f - fresnel) * (1.0f - metallic);
        const float3 radiance = hemi * 0.52f;
        direct_lighting += (kd * albedo / 3.14159265359f + specular) * radiance * ndotl;
    }

    const float3 point_vec = frame.point_light_pos.xyz - in.world_position;
    const float point_dist = length(point_vec);
    const float3 point_dir = point_dist > 1e-4f ? point_vec / point_dist : float3(0.0f, 1.0f, 0.0f);
    const float point_radius = max(frame.point_light_pos.w, 1.0f);
    const float point_atten = clamp(1.0f - point_dist / point_radius, 0.0f, 1.0f);
    const float point_ndotl = max(dot(normal_ws, point_dir), 0.0f);
    if (point_ndotl > 0.0f && point_atten > 0.0f)
    {
        const float3 half_vec = normalize(view_dir + point_dir);
        const float3 fresnel = fresnel_schlick(max(dot(half_vec, view_dir), 0.0f), f0);
        const float ndf = distribution_ggx(normal_ws, half_vec, roughness);
        const float geometry = geometry_smith(normal_ws, view_dir, point_dir, roughness);
        const float3 specular = (ndf * geometry * fresnel)
            / max(4.0f * max(dot(normal_ws, view_dir), 0.0f) * point_ndotl, 1e-4f);
        const float3 kd = (1.0f - fresnel) * (1.0f - metallic);
        const float3 radiance = float3(1.05f, 0.98f, 0.90f)
            * max(frame.render_tuning.y, 0.0f) * point_atten * point_atten;
        direct_lighting += (kd * albedo / 3.14159265359f + specular) * radiance * point_ndotl;
    }

    float3 shaded = albedo * (hemi * ambient * combined_ao) + direct_lighting;
    if (in.tex_blend > 0.5f)
    {
        const float4 label = signAtlas.sample(signSampler, in.atlas_uv);
        const float2 atlas_texels_per_pixel = max(
            fwidth(in.atlas_uv) * float2(signAtlas.get_width(), signAtlas.get_height()),
            float2(1e-5f));
        const float2 projected_ink_pixels = in.label_ink_pixel_size / atlas_texels_per_pixel;
        const float projected_text_pixels = min(projected_ink_pixels.x, projected_ink_pixels.y);
        const float fade_start_px = max(min(frame.label_fade_px.x, frame.label_fade_px.y), 0.0f);
        const float fade_end_px = max(max(frame.label_fade_px.x, frame.label_fade_px.y), fade_start_px + 1e-3f);
        const float visibility = smoothstep(fade_start_px, fade_end_px, projected_text_pixels);
        const float label_alpha = smoothstep(0.18f, 0.55f, label.a) * visibility;
        shaded = mix(shaded, label.rgb, label_alpha);
    }

    const float output_gamma = max(frame.render_tuning.x, 1.0f);
    const float3 encoded = pow(max(shaded, float3(0.0f)), float3(1.0f / output_gamma));
    return float4(encoded, 1.0f);
}
