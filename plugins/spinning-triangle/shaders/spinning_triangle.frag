#version 450

layout(location = 0) in vec3 color;
layout(location = 0) out vec4 out_color;

void main() {
#ifdef DRAXUL_RELOAD_RENDER_VARIANT
    out_color = vec4(1.0 - color, 1.0);
#else
    out_color = vec4(color, 1.0);
#endif
}
