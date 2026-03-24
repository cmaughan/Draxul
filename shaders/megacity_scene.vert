#version 450

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    vec4 light_dir;
} frame;

layout(push_constant) uniform ObjectUniforms {
    mat4 world;
    vec4 color;
} object_data;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;

layout(location = 0) out vec3 out_color;

void main()
{
    vec4 world_position = object_data.world * vec4(in_position, 1.0);
    vec3 normal_ws = normalize(mat3(object_data.world) * in_normal);
    vec3 light_dir = normalize(-frame.light_dir.xyz);
    float ndotl = max(dot(normal_ws, light_dir), 0.0);
    float lighting = 0.2 + 0.8 * ndotl;

    out_color = in_color * object_data.color.rgb * lighting;
    gl_Position = frame.proj * frame.view * world_position;
}
