#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) out vec2 out_ndc;

vec2 fullscreen_corner(int vertex)
{
    if (vertex == 0)
        return vec2(-1.0, -1.0);
    if (vertex == 1)
        return vec2(-1.0, 3.0);
    return vec2(3.0, -1.0);
}

void main()
{
    vec2 ndc = fullscreen_corner(gl_VertexIndex);
    out_ndc = ndc;
    gl_Position = vec4(ndc, 1.0, 1.0);
}
