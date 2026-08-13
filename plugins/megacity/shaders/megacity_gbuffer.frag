#version 450

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
layout(set = 0, binding = 4) uniform sampler2D material_textures[25];

layout(location = 0) in vec3 in_normal_ws;
layout(location = 1) flat in uint in_material_index;
layout(location = 2) in vec2 in_material_uv;
layout(location = 0) out vec4 out_normal; // RG = octahedral normal, BA reserved

const uint kShadingLeafCutoutPbr = 3u;
const float kLeafAlphaCutoff = 0.35;

vec4 sample_material_texture(uint texture_index, vec2 uv)
{
    return texture(material_textures[int(texture_index)], uv);
}

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
    MaterialInstance material = material_table.materials[min(in_material_index, 63u)];
    if (material.metadata.x == kShadingLeafCutoutPbr)
    {
        vec2 material_uv = in_material_uv * material.scalar_params.x;
        float opacity = sample_material_texture(material.texture_indices.w, material_uv).r;
        if (opacity < kLeafAlphaCutoff)
            discard;
    }

    vec3 normal_ws = normalize(in_normal_ws);
    out_normal = vec4(oct_encode(normal_ws), 0.0, 1.0);
}
