#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) in vec4 in_position0_size;
layout(location = 1) in vec4 in_position1_selected;
layout(location = 2) in vec4 in_color;

layout(location = 0) out vec4 out_color;

vec3 camera_right(vec3 forward)
{
    vec3 right = cross(forward, vec3(0.0, 1.0, 0.0));
    if (dot(right, right) < 0.000001)
        return vec3(1.0, 0.0, 0.0);
    return normalize(right);
}

void main()
{
    int segment = gl_VertexIndex / 2;
    int endpoint = gl_VertexIndex & 1;
    float endpoint_sign = endpoint == 0 ? -1.0 : 1.0;
    float selected = in_position1_selected.w;
    float alpha = clamp(push.render_params.w, 0.0, 1.0);

    vec3 forward = normalize(-push.camera_pos.xyz);
    vec3 right = camera_right(forward);
    vec3 up = normalize(cross(right, forward));
    vec3 axis = segment == 0 ? right
        : segment == 1 ? up
        : segment == 2 ? normalize(right + up)
        : normalize(right - up);

    vec3 center = mix(in_position0_size.xyz, in_position1_selected.xyz, alpha);
    vec3 world = center + axis * in_position0_size.w * endpoint_sign;
    out_color = in_color;
    if (segment >= 2 && selected < 0.5)
        out_color.a = 0.0;
    gl_Position = push.view_proj * vec4(world, 1.0);
}
