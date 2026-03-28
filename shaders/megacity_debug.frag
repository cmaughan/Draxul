#version 450

layout(set = 0, binding = 0) uniform FrameUniforms
{
    mat4 view;
    mat4 proj;
    mat4 inv_view_proj;
    vec4 camera_pos;
    vec4 light_dir;
    vec4 point_light_pos;
    vec4 label_fade_px;
    vec4 render_tuning;
    vec4 screen_params;
    vec4 ao_params;
    vec4 debug_view;
    vec4 world_debug_bounds;
}
frame;
struct MaterialInstance
{
    vec4 scalar_params;
    uvec4 texture_indices;
    uvec4 metadata;
};

layout(set = 0, binding = 1) uniform MaterialUniforms
{
    MaterialInstance materials[64];
}
material_table;
layout(set = 0, binding = 2) uniform sampler2D sign_atlas;
layout(set = 0, binding = 3) uniform sampler2D ao_buffer;
layout(set = 0, binding = 4) uniform sampler2D material_textures[25];

layout(location = 0) in vec3 in_normal_ws;
layout(location = 1) in vec3 in_base_color;
layout(location = 2) in vec3 in_world_position;
layout(location = 3) in vec2 in_atlas_uv;
layout(location = 4) in float in_tex_blend;
layout(location = 5) in vec2 in_label_ink_pixel_size;
layout(location = 6) flat in uint in_material_index;
layout(location = 7) in vec2 in_material_uv;
layout(location = 8) in vec4 in_tangent_ws;
layout(location = 9) in float in_opacity;
layout(location = 0) out vec4 out_frag_color;

// Debug view mode indices (matches MegaCityDebugView enum):
// 0 = FinalScene (should not reach this shader)
// 1 = AmbientOcclusion (raw)
// 2 = AmbientOcclusionDenoised
// 3 = Normals
// 4 = WorldPosition
// 5 = Roughness
// 6 = Metallic
// 7 = Albedo
// 8 = Tangents
// 9 = UV
// 10 = Depth
// 11 = Bitangents
// 12 = TBN Packed

const uint kShadingFlatColor = 0u;
const uint kShadingTexturedTintedPbr = 1u;
const uint kShadingVertexTintPbr = 2u;
const uint kShadingLeafCutoutPbr = 3u;
const float kLeafAlphaCutoff = 0.35;

mat3 tangent_basis(vec3 normal_ws, vec4 tangent_ws)
{
    vec3 tangent = normalize(tangent_ws.xyz);
    vec3 bitangent = normalize(cross(normal_ws, tangent) * tangent_ws.w);
    return mat3(tangent, bitangent, normal_ws);
}

vec4 sample_material_texture(uint texture_index, vec2 uv)
{
    return texture(material_textures[int(texture_index)], uv);
}

vec3 world_position_false_color(vec3 world_pos)
{
    float min_x = frame.world_debug_bounds.x;
    float max_x = frame.world_debug_bounds.y;
    float min_z = frame.world_debug_bounds.z;
    float max_z = frame.world_debug_bounds.w;
    float span_x = max(max_x - min_x, 1e-3);
    float span_z = max(max_z - min_z, 1e-3);
    float span_y = max(max(span_x, span_z) * 0.35, 8.0);
    return clamp(
        vec3(
            (world_pos.x - min_x) / span_x,
            world_pos.y / span_y,
            (world_pos.z - min_z) / span_z),
        vec3(0.0),
        vec3(1.0));
}

void main()
{
    int mode = int(floor(frame.debug_view.x + 0.5));

    vec3 normal_ws = normalize(in_normal_ws);
    vec3 albedo = in_base_color;
    float roughness = 0.65;
    float metallic = 0.0;

    MaterialInstance material = material_table.materials[min(in_material_index, 63u)];
    vec2 material_uv = in_material_uv * material.scalar_params.x;
    float normal_strength = max(material.scalar_params.y, 0.0);
    metallic = material.scalar_params.w;
    if (material.metadata.x == kShadingFlatColor)
        roughness = clamp(material.scalar_params.x, 0.04, 1.0);

    if (material.metadata.x == kShadingTexturedTintedPbr)
    {
        vec3 tangent_normal = sample_material_texture(material.texture_indices.y, material_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);
        mat3 tbn = tangent_basis(normal_ws, in_tangent_ws);
        normal_ws = normalize(tbn * tangent_normal);
        albedo = sample_material_texture(material.texture_indices.x, material_uv).rgb * in_base_color;
        roughness = clamp(sample_material_texture(material.texture_indices.z, material_uv).r, 0.04, 1.0);
    }
    else if (material.metadata.x == kShadingVertexTintPbr)
    {
        mat3 tbn = tangent_basis(normal_ws, in_tangent_ws);
        vec3 tangent_normal = sample_material_texture(material.texture_indices.y, material_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);
        normal_ws = normalize(tbn * tangent_normal);
        albedo = sample_material_texture(material.texture_indices.x, material_uv).rgb * in_base_color;
        roughness = clamp(sample_material_texture(material.texture_indices.z, material_uv).r, 0.04, 1.0);
        metallic = clamp(material.scalar_params.w * sample_material_texture(material.metadata.y, material_uv).r, 0.0, 1.0);
    }
    else if (material.metadata.x == kShadingLeafCutoutPbr)
    {
        float opacity = sample_material_texture(material.texture_indices.w, material_uv).r;
        if (opacity < kLeafAlphaCutoff)
            discard;

        vec3 tangent_normal = sample_material_texture(material.texture_indices.y, material_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);
        mat3 tbn = tangent_basis(normal_ws, in_tangent_ws);
        normal_ws = normalize(tbn * tangent_normal);
        albedo = sample_material_texture(material.texture_indices.x, material_uv).rgb * in_base_color;
        roughness = clamp(sample_material_texture(material.texture_indices.z, material_uv).r, 0.04, 1.0);
    }

    vec2 screen_uv = clamp(
        (gl_FragCoord.xy - frame.screen_params.xy) * frame.screen_params.zw,
        vec2(0.0),
        vec2(1.0));

    vec3 result = vec3(0.0);

    if (mode == 1 || mode == 2)
    {
        // AO: mode 1 = raw (ao_buffer already has raw or denoised depending on binding),
        // mode 2 = denoised
        result = texture(ao_buffer, screen_uv).rrr;
    }
    else if (mode == 3)
    {
        result = normal_ws * 0.5 + 0.5;
    }
    else if (mode == 4)
    {
        result = world_position_false_color(in_world_position);
    }
    else if (mode == 5)
    {
        result = vec3(roughness);
    }
    else if (mode == 6)
    {
        result = vec3(metallic);
    }
    else if (mode == 7)
    {
        result = albedo;
    }
    else if (mode == 8)
    {
        vec3 tangent = normalize(in_tangent_ws.xyz);
        result = tangent * 0.5 + 0.5;
    }
    else if (mode == 9)
    {
        result = vec3(in_material_uv, 0.0);
    }
    else if (mode == 10)
    {
        float linear_depth = gl_FragCoord.z;
        result = vec3(linear_depth);
    }
    else if (mode == 11)
    {
        vec3 tangent = normalize(in_tangent_ws.xyz);
        vec3 bitangent = normalize(cross(normal_ws, tangent) * in_tangent_ws.w);
        result = bitangent * 0.5 + 0.5;
    }
    else if (mode == 12)
    {
        vec3 tangent = normalize(in_tangent_ws.xyz);
        vec3 bitangent = normalize(cross(normal_ws, tangent) * in_tangent_ws.w);
        result = vec3(
            tangent.x * 0.5 + 0.5,
            bitangent.y * 0.5 + 0.5,
            normal_ws.z * 0.5 + 0.5);
    }

    out_frag_color = vec4(result, in_opacity);
}
