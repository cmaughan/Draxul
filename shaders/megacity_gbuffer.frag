#version 450

layout(location = 0) in vec3 in_normal_ws;
layout(location = 0) out vec4 out_normal;      // RG = octahedral normal, BA reserved

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
    out_normal = vec4(oct_encode(normal_ws), 0.0, 1.0);
}
