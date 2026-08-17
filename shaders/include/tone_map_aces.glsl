// Narkowicz ACES filmic tone-map curve — GLSL (Vulkan) side.
//
// SINGLE SOURCE OF TRUTH: shaders/contracts/tone_map_aces.toml.
// The MSL twin is shaders/include/tone_map_aces.metal; both are cross-checked
// against the manifest by tests/shader_abi_parity_tests.cpp, so editing one
// language alone fails the [shader_abi] test.
//
// Include from a product fragment shader with:
//     #include "tone_map_aces.glsl"
// The product's CMake passes -I <draxul>/shaders/include to glslc.

#ifndef DRAXUL_TONE_MAP_ACES_GLSL
#define DRAXUL_TONE_MAP_ACES_GLSL

vec3 tone_map_aces(vec3 hdr, float exposure, float white_point)
{
    vec3 color = max(hdr, vec3(0.0)) * max(exposure, 0.0);
    color /= max(white_point, 1e-3);
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), vec3(0.0), vec3(1.0));
}

#endif // DRAXUL_TONE_MAP_ACES_GLSL
