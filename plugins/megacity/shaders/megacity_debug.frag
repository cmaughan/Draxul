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
    vec4 perf_tuning;
    vec4 screen_params;
    vec4 ao_params;
    vec4 debug_view;
    vec4 world_debug_bounds;
    mat4 shadow_view_proj[3];
    mat4 shadow_texture_matrix[3];
    vec4 shadow_split_depths;
    vec4 shadow_params;
    mat4 point_shadow_view_proj[6];
    mat4 point_shadow_texture_matrix[6];
    vec4 point_shadow_params;
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
layout(set = 0, binding = 5) uniform sampler2DShadow shadow_maps[3];
layout(set = 0, binding = 6) uniform sampler2D point_shadow_maps[6];

layout(location = 0) in vec3 in_normal_ws;
layout(location = 1) in vec3 in_base_color;
layout(location = 2) in vec3 in_world_position;
layout(location = 3) in vec2 in_atlas_uv;
layout(location = 4) in float in_tex_blend;
layout(location = 5) in vec4 in_label_metrics;
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
// 13 = Directional Shadow
// 14 = Point Shadow
// 15 = Point Shadow Face
// 16 = Point Shadow Stored Depth
// 17 = Point Shadow Depth Delta

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

int shadow_cascade_index(float clip_depth)
{
    if (clip_depth <= frame.shadow_split_depths.x)
        return 0;
    if (clip_depth <= frame.shadow_split_depths.y)
        return 1;
    return 2;
}

float sample_shadow_map(int cascade_index, vec3 shadow_coord)
{
    if (shadow_coord.x <= 0.0 || shadow_coord.x >= 1.0 || shadow_coord.y <= 0.0 || shadow_coord.y >= 1.0)
        return 1.0;
    if (shadow_coord.z <= 0.0 || shadow_coord.z >= 1.0)
        return 1.0;

    float visibility = 0.0;
    float texel = max(frame.shadow_params.w, 1e-6);
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 uv = shadow_coord.xy + vec2(x, y) * texel;
            visibility += texture(shadow_maps[cascade_index], vec3(uv, shadow_coord.z));
        }
    }
    return visibility / 9.0;
}

float directional_shadow_visibility(vec3 world_position, vec3 normal_ws, float ndotl, float clip_depth)
{
    int cascade_index = shadow_cascade_index(clip_depth);
    float normal_bias = max(frame.shadow_params.z, 0.0);
    vec3 biased_world = world_position + normal_ws * (normal_bias * (1.0 - ndotl));
    vec4 shadow_position = frame.shadow_texture_matrix[cascade_index] * vec4(biased_world, 1.0);
    vec3 shadow_coord = shadow_position.xyz / max(shadow_position.w, 1e-6);
    shadow_coord.z -= max(frame.shadow_params.y, 0.0);
    return sample_shadow_map(cascade_index, shadow_coord);
}

float point_shadow_visibility(vec3 world_position, vec3 normal_ws, vec3 point_dir)
{
    if (frame.point_shadow_params.w < 0.5)
        return 1.0;

    float radius = max(frame.point_light_pos.w, 1.0);
    float sample_bias = max(frame.point_shadow_params.x, 0.0);
    float normal_bias = max(frame.point_shadow_params.y, 0.0);
    vec3 light_to_surface = world_position - frame.point_light_pos.xyz;
    float current_depth = length(light_to_surface) / radius;
    vec3 abs_dir = abs(light_to_surface);
    int face_index = 0;
    if (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z)
        face_index = light_to_surface.x >= 0.0 ? 0 : 1;
    else if (abs_dir.y >= abs_dir.x && abs_dir.y >= abs_dir.z)
        face_index = light_to_surface.y >= 0.0 ? 2 : 3;
    else
        face_index = light_to_surface.z >= 0.0 ? 4 : 5;

    vec4 shadow_position = frame.point_shadow_texture_matrix[face_index] * vec4(world_position, 1.0);
    vec3 shadow_coord = shadow_position.xyz / max(shadow_position.w, 1e-6);
    if (shadow_coord.x <= 0.0 || shadow_coord.x >= 1.0 || shadow_coord.y <= 0.0 || shadow_coord.y >= 1.0)
        return 1.0;
    if (shadow_coord.z <= 0.0 || shadow_coord.z >= 1.0)
        return 1.0;

    float stored_depth = texture(point_shadow_maps[face_index], shadow_coord.xy).r;
    float slope_bias = normal_bias * (1.0 - max(dot(normal_ws, point_dir), 0.0));
    return current_depth - (sample_bias + slope_bias) <= stored_depth ? 1.0 : 0.0;
}

int point_shadow_face_index(vec3 light_to_surface)
{
    vec3 abs_dir = abs(light_to_surface);
    if (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z)
        return light_to_surface.x >= 0.0 ? 0 : 1;
    if (abs_dir.y >= abs_dir.x && abs_dir.y >= abs_dir.z)
        return light_to_surface.y >= 0.0 ? 2 : 3;
    return light_to_surface.z >= 0.0 ? 4 : 5;
}

float point_shadow_stored_depth(vec3 world_position)
{
    vec3 light_to_surface = world_position - frame.point_light_pos.xyz;
    int face_index = point_shadow_face_index(light_to_surface);
    vec4 shadow_position = frame.point_shadow_texture_matrix[face_index] * vec4(world_position, 1.0);
    vec3 shadow_coord = shadow_position.xyz / max(shadow_position.w, 1e-6);
    if (shadow_coord.x <= 0.0 || shadow_coord.x >= 1.0 || shadow_coord.y <= 0.0 || shadow_coord.y >= 1.0)
        return 1.0;
    if (shadow_coord.z <= 0.0 || shadow_coord.z >= 1.0)
        return 1.0;
    return texture(point_shadow_maps[face_index], shadow_coord.xy).r;
}

float point_shadow_current_depth(vec3 world_position)
{
    vec3 light_to_surface = world_position - frame.point_light_pos.xyz;
    return length(light_to_surface) / max(frame.point_light_pos.w, 1.0);
}

vec3 point_shadow_face_color(int face_index)
{
    switch (face_index)
    {
    case 0: return vec3(1.0, 0.2, 0.2);
    case 1: return vec3(1.0, 0.7, 0.2);
    case 2: return vec3(0.2, 1.0, 0.2);
    case 3: return vec3(0.2, 1.0, 1.0);
    case 4: return vec3(0.2, 0.4, 1.0);
    default: return vec3(1.0, 0.2, 1.0);
    }
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
    else if (mode == 13)
    {
        vec3 dir_light = normalize(-frame.light_dir.xyz);
        float ndotl = max(dot(normal_ws, dir_light), 0.0);
        float visibility = ndotl > 0.0
            ? directional_shadow_visibility(in_world_position, normal_ws, ndotl, gl_FragCoord.z)
            : 1.0;
        result = vec3(visibility);
    }
    else if (mode == 14)
    {
        vec3 point_vec = frame.point_light_pos.xyz - in_world_position;
        float point_dist = length(point_vec);
        vec3 point_dir = point_dist > 1e-4 ? point_vec / point_dist : vec3(0.0, 1.0, 0.0);
        result = vec3(point_shadow_visibility(in_world_position, normal_ws, point_dir));
    }
    else if (mode == 15)
    {
        result = point_shadow_face_color(point_shadow_face_index(in_world_position - frame.point_light_pos.xyz));
    }
    else if (mode == 16)
    {
        result = vec3(point_shadow_stored_depth(in_world_position));
    }
    else if (mode == 17)
    {
        float delta = point_shadow_current_depth(in_world_position) - point_shadow_stored_depth(in_world_position);
        result = delta >= 0.0
            ? vec3(clamp(delta * 12.0, 0.0, 1.0), 0.0, 0.0)
            : vec3(0.0, clamp(-delta * 12.0, 0.0, 1.0), 0.0);
    }

    out_frag_color = vec4(result, in_opacity);
}
