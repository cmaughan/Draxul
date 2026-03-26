#version 450

layout(set = 0, binding = 4) uniform sampler2D road_albedo_texture;
layout(set = 0, binding = 5) uniform sampler2D road_normal_texture;
layout(set = 0, binding = 6) uniform sampler2D road_roughness_texture;
layout(set = 0, binding = 7) uniform sampler2D road_ao_texture;

layout(location = 0) in vec3 in_normal_ws;
layout(location = 1) in vec3 in_base_color;
layout(location = 2) in vec3 in_world_position;
layout(location = 3) flat in vec4 in_material_info;

layout(location = 0) out vec4 out_material;    // RG = octahedral normal, B = roughness, A = material AO
layout(location = 1) out vec4 out_base_color;  // RGB = albedo, A = metallic
layout(location = 2) out vec4 out_ao;          // R = ambient occlusion, GBA = reserved

// Octahedral encoding: unit normal -> [0,1]^2
// Reference: "Survey of Efficient Representations for Independent Unit Vectors" (Cigolle et al. 2014)
vec2 oct_encode(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * mix(vec2(-1.0), vec2(1.0), greaterThanEqual(n.xy, vec2(0.0)));
    return n.xy * 0.5 + 0.5;
}

void main()
{
    vec3 normal_ws = normalize(in_normal_ws);
    vec3 albedo = in_base_color;
    float roughness = 0.5;
    float metallic = 0.0;
    float material_ao = 1.0;

    if (in_material_info.x > 0.5)
    {
        vec2 road_uv = in_world_position.xz * in_material_info.y;
        vec3 tangent_normal = texture(road_normal_texture, road_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= max(in_material_info.z, 0.0);
        tangent_normal = normalize(tangent_normal);

        vec3 t = vec3(1.0, 0.0, 0.0);
        vec3 b = vec3(0.0, 0.0, 1.0);
        mat3 tbn = mat3(t, b, normal_ws);

        normal_ws = normalize(tbn * tangent_normal);
        albedo = texture(road_albedo_texture, road_uv).rgb * in_base_color;
        roughness = clamp(texture(road_roughness_texture, road_uv).r, 0.04, 1.0);
        material_ao = mix(1.0, texture(road_ao_texture, road_uv).r, clamp(in_material_info.w, 0.0, 1.0));
    }

    out_material = vec4(oct_encode(normalize(normal_ws)), roughness, material_ao);
    out_base_color = vec4(albedo, metallic);
    out_ao = vec4(1.0, 0.0, 0.0, 1.0);
}
