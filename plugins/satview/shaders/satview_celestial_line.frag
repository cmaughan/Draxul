#version 450

layout(location = 0) in vec4 in_color;
layout(location = 1) noperspective in float in_line_distance;
layout(location = 2) noperspective in float in_line_across;
layout(location = 3) flat in vec2 in_dash_gap;
layout(location = 0) out vec4 out_color;

void main()
{
    float edge_width = max(fwidth(in_line_across), 0.02);
    float edge_alpha = 1.0 - smoothstep(1.0 - edge_width, 1.0, abs(in_line_across));
    float dash_alpha = 1.0;
    if (in_dash_gap.x > 0.0 && in_dash_gap.y > 0.0)
    {
        float period = in_dash_gap.x + in_dash_gap.y;
        float position = mod(in_line_distance, period);
        float dash_edge = max(fwidth(in_line_distance), 0.5);
        dash_alpha = 1.0 - smoothstep(
            in_dash_gap.x - dash_edge,
            in_dash_gap.x + dash_edge,
            position);
    }
    out_color = vec4(in_color.rgb, in_color.a * edge_alpha * dash_alpha);
}
