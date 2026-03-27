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
layout(set = 0, binding = 4) uniform sampler2D material_textures[24];

layout(location = 0) in vec3 in_normal_ws;
layout(location = 1) in vec3 in_base_color;
layout(location = 2) in vec3 in_world_position;
layout(location = 3) in vec2 in_atlas_uv;
layout(location = 4) in float in_tex_blend;
layout(location = 5) in vec2 in_label_ink_pixel_size;
layout(location = 6) flat in uint in_material_index;
layout(location = 7) in vec2 in_material_uv;
layout(location = 8) in vec4 in_tangent_ws;
layout(location = 0) out vec4 out_frag_color;

const float kPi = 3.14159265359;
const uint kShadingFlatColor = 0u;
const uint kShadingTexturedTintedPbr = 1u;
const uint kShadingVertexTintPbr = 2u;
const uint kShadingLeafCutoutPbr = 3u;
const float kLeafAlphaCutoff = 0.35;

float distribution_ggx(vec3 n, vec3 h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = max(dot(n, h), 0.0);
    float ndoth2 = ndoth * ndoth;
    float denom = ndoth2 * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * denom * denom, 1e-4);
}

float geometry_schlick_ggx(float ndotv, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return ndotv / max(ndotv * (1.0 - k) + k, 1e-4);
}

float geometry_smith(vec3 n, vec3 v, vec3 l, float roughness)
{
    return geometry_schlick_ggx(max(dot(n, v), 0.0), roughness)
        * geometry_schlick_ggx(max(dot(n, l), 0.0), roughness);
}

vec3 fresnel_schlick(float cos_theta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

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

void main()
{
    vec3 normal_ws = normalize(in_normal_ws);
    vec3 albedo = in_base_color;
    float roughness = 0.65;
    float metallic = 0.0;
    float material_ao = 1.0;
    float leaf_scattering = 0.0;
    MaterialInstance material = material_table.materials[min(in_material_index, 63u)];
    vec2 material_uv = in_material_uv * material.scalar_params.x;
    float normal_strength = max(material.scalar_params.y, 0.0);
    float ao_strength = clamp(material.scalar_params.z, 0.0, 1.0);
    metallic = material.scalar_params.w;

    if (material.metadata.x == kShadingTexturedTintedPbr)
    {
        vec3 tangent_normal = sample_material_texture(material.texture_indices.y, material_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);
        mat3 tbn = tangent_basis(normal_ws, in_tangent_ws);

        normal_ws = normalize(tbn * tangent_normal);
        albedo = sample_material_texture(material.texture_indices.x, material_uv).rgb * in_base_color;
        roughness = clamp(sample_material_texture(material.texture_indices.z, material_uv).r, 0.04, 1.0);
        material_ao = mix(1.0, sample_material_texture(material.texture_indices.w, material_uv).r, ao_strength);
    }
    else if (material.metadata.x == kShadingVertexTintPbr)
    {
        mat3 tbn = tangent_basis(normal_ws, in_tangent_ws);
        vec3 tangent_normal = sample_material_texture(material.texture_indices.y, material_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);

        normal_ws = normalize(tbn * tangent_normal);
        albedo = in_base_color;
        roughness = clamp(sample_material_texture(material.texture_indices.z, material_uv).r, 0.04, 1.0);
        material_ao = mix(1.0, sample_material_texture(material.texture_indices.w, material_uv).r, ao_strength);
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
        leaf_scattering = sample_material_texture(material.metadata.y, material_uv).r * ao_strength;
    }

    vec2 screen_uv = clamp(
        (gl_FragCoord.xy - frame.screen_params.xy) * frame.screen_params.zw,
        vec2(0.0),
        vec2(1.0));
    float ao = clamp(texture(ao_buffer, screen_uv).r, 0.0, 1.0);
    vec3 view_dir = normalize(frame.camera_pos.xyz - in_world_position);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    float ambient = max(frame.render_tuning.z, 0.0);
    float hemi_factor = normal_ws.y * 0.5 + 0.5;
    vec3 hemi = mix(vec3(0.84, 0.82, 0.78), vec3(1.04, 1.03, 1.01), hemi_factor);
    float combined_ao = material_ao * ao;
    vec3 ambient_lighting = hemi * ambient * combined_ao;

    vec3 direct_lighting = vec3(0.0);

    vec3 dir_light = normalize(-frame.light_dir.xyz);
    float ndotl = max(dot(normal_ws, dir_light), 0.0);
    if (ndotl > 0.0)
    {
        vec3 half_vec = normalize(view_dir + dir_light);
        vec3 fresnel = fresnel_schlick(max(dot(half_vec, view_dir), 0.0), f0);
        float ndf = distribution_ggx(normal_ws, half_vec, roughness);
        float geometry = geometry_smith(normal_ws, view_dir, dir_light, roughness);
        vec3 numerator = ndf * geometry * fresnel;
        float denom = max(4.0 * max(dot(normal_ws, view_dir), 0.0) * ndotl, 1e-4);
        vec3 specular = numerator / denom;
        vec3 kd = (1.0 - fresnel) * (1.0 - metallic);
        vec3 radiance = hemi * 0.52;
        direct_lighting += (kd * albedo / kPi + specular) * radiance * ndotl;
        if (leaf_scattering > 0.0)
        {
            float transmitted = max(dot(-normal_ws, dir_light), 0.0);
            direct_lighting += albedo * radiance * (leaf_scattering * 0.45) * transmitted;
        }
    }

    vec3 point_vec = frame.point_light_pos.xyz - in_world_position;
    float point_dist = length(point_vec);
    vec3 point_dir = point_dist > 1e-4 ? point_vec / point_dist : vec3(0.0, 1.0, 0.0);
    float point_radius = max(frame.point_light_pos.w, 1.0);
    float point_atten = clamp(1.0 - point_dist / point_radius, 0.0, 1.0);
    float point_ndotl = max(dot(normal_ws, point_dir), 0.0);
    if (point_ndotl > 0.0 && point_atten > 0.0)
    {
        vec3 half_vec = normalize(view_dir + point_dir);
        vec3 fresnel = fresnel_schlick(max(dot(half_vec, view_dir), 0.0), f0);
        float ndf = distribution_ggx(normal_ws, half_vec, roughness);
        float geometry = geometry_smith(normal_ws, view_dir, point_dir, roughness);
        vec3 numerator = ndf * geometry * fresnel;
        float denom = max(4.0 * max(dot(normal_ws, view_dir), 0.0) * point_ndotl, 1e-4);
        vec3 specular = numerator / denom;
        vec3 kd = (1.0 - fresnel) * (1.0 - metallic);
        vec3 radiance = vec3(1.05, 0.98, 0.90)
            * max(frame.render_tuning.y, 0.0) * point_atten * point_atten;
        direct_lighting += (kd * albedo / kPi + specular) * radiance * point_ndotl;
        if (leaf_scattering > 0.0)
        {
            float transmitted = max(dot(-normal_ws, point_dir), 0.0);
            direct_lighting += albedo * radiance * (leaf_scattering * 0.60) * transmitted;
        }
    }

    vec3 shaded = albedo * ambient_lighting + direct_lighting;
    if (in_tex_blend > 0.5)
    {
        vec4 label = texture(sign_atlas, in_atlas_uv);
        vec2 atlas_texels_per_pixel = max(
            fwidth(in_atlas_uv) * vec2(textureSize(sign_atlas, 0)),
            vec2(1e-5));
        vec2 projected_ink_pixels = in_label_ink_pixel_size / atlas_texels_per_pixel;
        float projected_text_pixels = min(projected_ink_pixels.x, projected_ink_pixels.y);
        float fade_start_px = max(min(frame.label_fade_px.x, frame.label_fade_px.y), 0.0);
        float fade_end_px = max(max(frame.label_fade_px.x, frame.label_fade_px.y), fade_start_px + 1e-3);
        float visibility = smoothstep(fade_start_px, fade_end_px, projected_text_pixels);
        float label_alpha = smoothstep(0.18, 0.55, label.a) * visibility;
        shaded = mix(shaded, label.rgb, label_alpha);
    }

    float output_gamma = max(frame.render_tuning.x, 1.0);
    vec3 encoded = pow(max(shaded, vec3(0.0)), vec3(1.0 / output_gamma));
    out_frag_color = vec4(encoded, 1.0);
}
