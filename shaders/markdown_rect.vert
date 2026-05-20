#version 450
#extension GL_GOOGLE_include_directive : require

#include "quad_offsets_shared.h"

layout(push_constant) uniform PushConstants {
    float screen_w;
    float screen_h;
    float viewport_x;
    float viewport_y;
} pc;

struct RectInstance {
    vec4 rect;
    vec4 color;
};

layout(std430, set = 0, binding = 0) readonly buffer RectBuffer {
    RectInstance rects[];
};

layout(location = 0) out vec4 frag_color;

void main() {
    RectInstance instance = rects[gl_InstanceIndex];
    vec2 offsets[6] = vec2[](
        vec2(QUAD_OFFSET_0), vec2(QUAD_OFFSET_1), vec2(QUAD_OFFSET_2),
        vec2(QUAD_OFFSET_3), vec2(QUAD_OFFSET_4), vec2(QUAD_OFFSET_5)
    );

    vec2 offset = offsets[gl_VertexIndex];
    vec2 pos = instance.rect.xy + offset * instance.rect.zw + vec2(pc.viewport_x, pc.viewport_y);
    vec2 ndc = (pos / vec2(pc.screen_w, pc.screen_h)) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
    frag_color = instance.color;
}
