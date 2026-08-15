#version 450

layout(location = 0) out vec2 out_uv;

void main()
{
    vec2 uv = vec2(
        float((gl_VertexIndex << 1) & 2),
        float(gl_VertexIndex & 2));
    out_uv = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
