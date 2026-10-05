#version 450

// Port of TokenFu's coin shading (tokenfu shaders/coin.frag) plus a neutral
// style for agents without a provider mark. Keep this in sync with the Metal
// source in activity_coin_pass_metal.mm.

layout(location = 0) in vec3 normal;
layout(location = 1) in vec2 uv;
layout(location = 2) in float face;
layout(location = 3) flat in int style;
layout(location = 4) in float brightness;

layout(location = 0) out vec4 outColor;

float segmentDistance(vec2 point, vec2 a, vec2 b)
{
    vec2 pa = point - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

float claudeMark(vec2 point)
{
    float distanceToMark = 10.0;
    for (int ray = 0; ray < 4; ++ray)
    {
        float angle = 0.78539816339 * float(ray);
        vec2 direction = vec2(cos(angle), sin(angle));
        distanceToMark = min(distanceToMark,
            segmentDistance(point, -direction * 0.55, direction * 0.55));
    }
    return 1.0 - smoothstep(0.085, 0.14, distanceToMark);
}

float codexMark(vec2 point)
{
    float angle = atan(point.y, point.x);
    float radius = length(point);
    float loopRadius = 0.47 + 0.075 * cos(angle * 6.0);
    float ring = 1.0 - smoothstep(0.07, 0.13, abs(radius - loopRadius));
    float hub = 1.0 - smoothstep(0.17, 0.23, radius);
    return max(ring, hub * 0.72);
}

// Grok's mark is a circular stroke crossed by an ascending slash.
float grokMark(vec2 point)
{
    float radius = length(point);
    float ring = 1.0 - smoothstep(0.075, 0.13, abs(radius - 0.48));
    float slashDistance = segmentDistance(
        point, vec2(-0.64, 0.55), vec2(0.64, -0.55));
    float slash = 1.0 - smoothstep(0.075, 0.13, slashDistance);
    return max(ring, slash);
}

float neutralMark(vec2 point)
{
    float radius = length(point);
    float ring = 1.0 - smoothstep(0.075, 0.13, abs(radius - 0.48));
    float hub = 1.0 - smoothstep(0.14, 0.2, radius);
    return max(ring, hub);
}

void main()
{
    vec3 n = normalize(normal);
    vec3 light = normalize(vec3(-0.45, -0.65, 1.0));
    vec3 viewDirection = vec3(0.0, 0.0, 1.0);
    float diffuse = 0.34 + 0.58 * max(dot(n, light), 0.0);
    float specular = pow(max(dot(reflect(-light, n), viewDirection), 0.0), 28.0) * 0.42;
    float rim = pow(1.0 - abs(dot(n, viewDirection)), 2.5) * 0.18;

    vec3 base = style == 0 ? vec3(0.22, 0.50, 0.88)
        : style == 1 ? vec3(1.0, 0.59, 0.43)
        : style == 2 ? vec3(0.48, 0.52, 0.60) : vec3(0.62, 0.64, 0.68);
    if (abs(face) < 0.5)
    {
        float grooves = 0.91 + 0.09 * sin(uv.x * 150.796447372);
        base *= 0.68 * grooves;
    }
    vec3 color = base * diffuse + vec3(specular + rim);

    if (abs(face) > 0.5)
    {
        vec2 point = (uv - 0.5) * 2.0;
        float mark = style == 0 ? codexMark(point)
            : style == 1 ? claudeMark(point)
            : style == 2 ? grokMark(point) : neutralMark(point);
        if (style == 0)
            color = mix(color, vec3(1.0), mark);
        else if (style == 1)
            color = mix(color, vec3(0.16, 0.075, 0.045) * (0.82 + specular), mark * 0.9);
        else if (style == 2)
            color = mix(color, vec3(0.96, 0.97, 1.0), mark);
        else
            color = mix(color, vec3(0.20, 0.21, 0.24), mark * 0.85);
    }
    outColor = vec4(color * brightness, 1.0);
}
