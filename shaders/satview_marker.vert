#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) in vec4 in_position0_size;
layout(location = 1) in vec4 in_position1_selected;
layout(location = 2) in vec4 in_color;

layout(location = 0) out vec4 out_color;

vec3 rotate_by_quaternion(vec3 value, vec4 quaternion)
{
    vec3 twice_cross = 2.0 * cross(quaternion.xyz, value);
    return value + quaternion.w * twice_cross + cross(quaternion.xyz, twice_cross);
}

void main()
{
    int segment = gl_VertexIndex / 2;
    int endpoint = gl_VertexIndex & 1;
    float endpoint_sign = endpoint == 0 ? -1.0 : 1.0;
    float selected = in_position1_selected.w;
    float alpha = clamp(push.render_params.w, 0.0, 1.0);

    vec3 right = normalize(rotate_by_quaternion(vec3(1.0, 0.0, 0.0), push.camera_orientation));
    vec3 up = normalize(rotate_by_quaternion(vec3(0.0, 1.0, 0.0), push.camera_orientation));
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
