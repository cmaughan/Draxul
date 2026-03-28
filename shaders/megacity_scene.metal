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
    uint material_index;
    float2 material_uv;
    float4 tangent_ws;
    float opacity;
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
    out.opacity = object.color.w;
    out.world_position = world_position.xyz;
    out.atlas_uv = mix(object.uv_rect.xy, object.uv_rect.zw, in.uv);
    out.tex_blend = in.tex_blend;
    out.label_ink_pixel_size = object.label_metrics.xy;
    out.material_index = object.material_data.x;
    out.material_uv = in.uv * object.uv_rect.zw;
    out.tangent_ws = float4(normalize(float3x3(object.world[0].xyz, object.world[1].xyz, object.world[2].xyz) * in.tangent.xyz), in.tangent.w);
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

constant uint kShadingFlatColor = 0u;
constant uint kShadingTexturedTintedPbr = 1u;
constant uint kShadingVertexTintPbr = 2u;
constant uint kShadingLeafCutoutPbr = 3u;
constant float kLeafAlphaCutoff = 0.35f;

float3x3 tangent_basis(float3 normal_ws, float4 tangent_ws)
{
    const float3 tangent = normalize(tangent_ws.xyz);
    const float3 bitangent = normalize(cross(normal_ws, tangent) * tangent_ws.w);
    return float3x3(tangent, bitangent, normal_ws);
}

fragment float4 scene_fragment(
    VertexOut in [[stage_in]],
    constant FrameUniforms& frame [[buffer(1)]],
    constant MaterialUniforms& materialTable [[buffer(3)]],
    texture2d<float> signAtlas [[texture(0)]],
    texture2d<float> aoTexture [[texture(1)]],
    array<texture2d<float>, 25> materialTextures [[texture(2)]],
    sampler signSampler [[sampler(0)]],
    sampler aoSampler [[sampler(1)]],
    sampler materialSampler [[sampler(2)]])
{
    float3 normal_ws = normalize(in.normal_ws);
    float3 albedo = in.base_color;
    float roughness = 0.65f;
    float metallic = 0.0f;
    float material_ao = 1.0f;
    float leaf_scattering = 0.0f;
    const MaterialInstance material = materialTable.materials[min(in.material_index, 63u)];
    const float2 material_uv = in.material_uv * material.scalar_params.x;
    const float normal_strength = max(material.scalar_params.y, 0.0f);
    const float ao_strength = clamp(material.scalar_params.z, 0.0f, 1.0f);
    metallic = material.scalar_params.w;
    if (material.metadata.x == kShadingFlatColor)
        roughness = clamp(material.scalar_params.x, 0.04f, 1.0f);

    if (material.metadata.x == kShadingTexturedTintedPbr)
    {
        float3 tangent_normal = materialTextures[material.texture_indices.y].sample(materialSampler, material_uv).xyz
                * 2.0f
            - 1.0f;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);
        const float3x3 tbn = tangent_basis(normal_ws, in.tangent_ws);
        normal_ws = normalize(tbn * tangent_normal);
        albedo = materialTextures[material.texture_indices.x].sample(materialSampler, material_uv).rgb
            * in.base_color;
        roughness = clamp(
            materialTextures[material.texture_indices.z].sample(materialSampler, material_uv).r,
            0.04f,
            1.0f);
        material_ao = mix(
            1.0f,
            materialTextures[material.texture_indices.w].sample(materialSampler, material_uv).r,
            ao_strength);
    }
    else if (material.metadata.x == kShadingLeafCutoutPbr)
    {
        const float opacity = materialTextures[material.texture_indices.w].sample(materialSampler, material_uv).r;
        if (opacity < kLeafAlphaCutoff)
            discard_fragment();

        float3 tangent_normal = materialTextures[material.texture_indices.y].sample(materialSampler, material_uv).xyz
                * 2.0f
            - 1.0f;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);
        const float3x3 tbn = tangent_basis(normal_ws, in.tangent_ws);
        normal_ws = normalize(tbn * tangent_normal);
        albedo = materialTextures[material.texture_indices.x].sample(materialSampler, material_uv).rgb
            * in.base_color;
        roughness = clamp(
            materialTextures[material.texture_indices.z].sample(materialSampler, material_uv).r,
            0.04f,
            1.0f);
        leaf_scattering = materialTextures[material.metadata.y].sample(materialSampler, material_uv).r
            * ao_strength;
    }
    else if (material.metadata.x == kShadingVertexTintPbr)
    {
        const float3x3 tbn = tangent_basis(normal_ws, in.tangent_ws);
        float3 tangent_normal = materialTextures[material.texture_indices.y].sample(materialSampler, material_uv).xyz
                * 2.0f
            - 1.0f;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);

        normal_ws = normalize(tbn * tangent_normal);
        albedo = materialTextures[material.texture_indices.x].sample(materialSampler, material_uv).rgb
            * in.base_color;
        roughness = clamp(
            materialTextures[material.texture_indices.z].sample(materialSampler, material_uv).r,
            0.04f,
            1.0f);
        material_ao = mix(
            1.0f,
            materialTextures[material.texture_indices.w].sample(materialSampler, material_uv).r,
            ao_strength);
        metallic = clamp(
            material.scalar_params.w
                * materialTextures[material.metadata.y].sample(materialSampler, material_uv).r,
            0.0f,
            1.0f);
    }

    const float2 screen_uv = clamp(
        (in.position.xy - frame.screen_params.xy) * frame.screen_params.zw,
        float2(0.0f),
        float2(1.0f));
    const float ao = clamp(aoTexture.sample(aoSampler, screen_uv).r, 0.0f, 1.0f);

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
        if (leaf_scattering > 0.0f)
        {
            const float transmitted = max(dot(-normal_ws, light_dir), 0.0f);
            direct_lighting += albedo * radiance * (leaf_scattering * 0.45f) * transmitted;
        }
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
        if (leaf_scattering > 0.0f)
        {
            const float transmitted = max(dot(-normal_ws, point_dir), 0.0f);
            direct_lighting += albedo * radiance * (leaf_scattering * 0.60f) * transmitted;
        }
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

    return float4(max(shaded, float3(0.0f)), in.opacity);
}

float3 world_position_false_color(constant FrameUniforms& frame, float3 world_pos)
{
    const float min_x = frame.world_debug_bounds.x;
    const float max_x = frame.world_debug_bounds.y;
    const float min_z = frame.world_debug_bounds.z;
    const float max_z = frame.world_debug_bounds.w;
    const float span_x = max(max_x - min_x, 1e-3f);
    const float span_z = max(max_z - min_z, 1e-3f);
    const float span_y = max(max(span_x, span_z) * 0.35f, 8.0f);
    return clamp(
        float3(
            (world_pos.x - min_x) / span_x,
            world_pos.y / span_y,
            (world_pos.z - min_z) / span_z),
        float3(0.0f),
        float3(1.0f));
}

fragment float4 debug_fragment(
    VertexOut in [[stage_in]],
    constant FrameUniforms& frame [[buffer(1)]],
    constant MaterialUniforms& materialTable [[buffer(3)]],
    texture2d<float> signAtlas [[texture(0)]],
    texture2d<float> aoTexture [[texture(1)]],
    array<texture2d<float>, 25> materialTextures [[texture(2)]],
    sampler signSampler [[sampler(0)]],
    sampler aoSampler [[sampler(1)]],
    sampler materialSampler [[sampler(2)]])
{
    const int mode = int(floor(frame.debug_view.x + 0.5f));

    float3 normal_ws = normalize(in.normal_ws);
    float3 albedo = in.base_color;
    float roughness = 0.65f;
    float metallic = 0.0f;

    const MaterialInstance material = materialTable.materials[min(in.material_index, 63u)];
    const float2 material_uv = in.material_uv * material.scalar_params.x;
    const float normal_strength = max(material.scalar_params.y, 0.0f);
    metallic = material.scalar_params.w;
    if (material.metadata.x == kShadingFlatColor)
        roughness = clamp(material.scalar_params.x, 0.04f, 1.0f);

    if (material.metadata.x == kShadingTexturedTintedPbr)
    {
        float3 tangent_normal = materialTextures[material.texture_indices.y].sample(materialSampler, material_uv).xyz
                * 2.0f
            - 1.0f;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);
        const float3x3 tbn = tangent_basis(normal_ws, in.tangent_ws);
        normal_ws = normalize(tbn * tangent_normal);
        albedo = materialTextures[material.texture_indices.x].sample(materialSampler, material_uv).rgb
            * in.base_color;
        roughness = clamp(
            materialTextures[material.texture_indices.z].sample(materialSampler, material_uv).r,
            0.04f,
            1.0f);
    }
    else if (material.metadata.x == kShadingVertexTintPbr)
    {
        const float3x3 tbn = tangent_basis(normal_ws, in.tangent_ws);
        float3 tangent_normal = materialTextures[material.texture_indices.y].sample(materialSampler, material_uv).xyz
                * 2.0f
            - 1.0f;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);
        normal_ws = normalize(tbn * tangent_normal);
        albedo = materialTextures[material.texture_indices.x].sample(materialSampler, material_uv).rgb
            * in.base_color;
        roughness = clamp(
            materialTextures[material.texture_indices.z].sample(materialSampler, material_uv).r,
            0.04f,
            1.0f);
        metallic = clamp(
            material.scalar_params.w
                * materialTextures[material.metadata.y].sample(materialSampler, material_uv).r,
            0.0f,
            1.0f);
    }
    else if (material.metadata.x == kShadingLeafCutoutPbr)
    {
        const float opacity = materialTextures[material.texture_indices.w].sample(materialSampler, material_uv).r;
        if (opacity < kLeafAlphaCutoff)
            discard_fragment();

        float3 tangent_normal = materialTextures[material.texture_indices.y].sample(materialSampler, material_uv).xyz
                * 2.0f
            - 1.0f;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);
        const float3x3 tbn = tangent_basis(normal_ws, in.tangent_ws);
        normal_ws = normalize(tbn * tangent_normal);
        albedo = materialTextures[material.texture_indices.x].sample(materialSampler, material_uv).rgb
            * in.base_color;
        roughness = clamp(
            materialTextures[material.texture_indices.z].sample(materialSampler, material_uv).r,
            0.04f,
            1.0f);
    }

    const float2 screen_uv = clamp(
        (in.position.xy - frame.screen_params.xy) * frame.screen_params.zw,
        float2(0.0f),
        float2(1.0f));

    float3 result = float3(0.0f);

    if (mode == 1 || mode == 2)
    {
        result = aoTexture.sample(aoSampler, screen_uv).rrr;
    }
    else if (mode == 3)
    {
        result = normal_ws * 0.5f + 0.5f;
    }
    else if (mode == 4)
    {
        result = world_position_false_color(frame, in.world_position);
    }
    else if (mode == 5)
    {
        result = float3(roughness);
    }
    else if (mode == 6)
    {
        result = float3(metallic);
    }
    else if (mode == 7)
    {
        result = albedo;
    }
    else if (mode == 8)
    {
        const float3 tangent = normalize(in.tangent_ws.xyz);
        result = tangent * 0.5f + 0.5f;
    }
    else if (mode == 9)
    {
        result = float3(in.material_uv, 0.0f);
    }
    else if (mode == 10)
    {
        result = float3(in.position.z);
    }
    else if (mode == 11)
    {
        const float3 tangent = normalize(in.tangent_ws.xyz);
        const float3 bitangent = normalize(cross(normal_ws, tangent) * in.tangent_ws.w);
        result = bitangent * 0.5f + 0.5f;
    }
    else if (mode == 12)
    {
        const float3 tangent = normalize(in.tangent_ws.xyz);
        const float3 bitangent = normalize(cross(normal_ws, tangent) * in.tangent_ws.w);
        result = float3(
            tangent.x * 0.5f + 0.5f,
            bitangent.y * 0.5f + 0.5f,
            normal_ws.z * 0.5f + 0.5f);
    }

    return float4(result, in.opacity);
}

struct FullscreenVertexOut
{
    float4 position [[position]];
    float2 uv;
};

vertex FullscreenVertexOut fullscreen_vertex(uint vertex_id [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(3.0f, -1.0f),
        float2(-1.0f, 3.0f)
    };
    FullscreenVertexOut out;
    out.position = float4(positions[vertex_id], 0.0f, 1.0f);
    out.uv = positions[vertex_id] * 0.5f + 0.5f;
    return out;
}

float3 tone_map_aces(float3 hdr, float exposure, float whitePoint)
{
    float3 color = max(hdr, float3(0.0f)) * max(exposure, 0.0f);
    color /= max(whitePoint, 1e-3f);
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), float3(0.0f), float3(1.0f));
}

fragment float4 scene_post_fragment(
    FullscreenVertexOut in [[stage_in]],
    constant FrameUniforms& frame [[buffer(0)]],
    texture2d<float> hdrScene [[texture(0)]],
    sampler linearSampler [[sampler(0)]])
{
    const float4 hdr = hdrScene.sample(linearSampler, in.uv);
    const float3 mapped = tone_map_aces(hdr.rgb, frame.render_tuning.x, frame.render_tuning.w);
    return float4(mapped, clamp(hdr.a, 0.0f, 1.0f));
}

fragment float4 scene_present_fragment(
    FullscreenVertexOut in [[stage_in]],
    texture2d<float> presentTexture [[texture(0)]],
    sampler linearSampler [[sampler(0)]])
{
    return presentTexture.sample(linearSampler, in.uv);
}
