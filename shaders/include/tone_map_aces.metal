// Narkowicz ACES filmic tone-map curve — MSL (Metal) side.
//
// SINGLE SOURCE OF TRUTH: shaders/contracts/tone_map_aces.toml.
// The GLSL twin is shaders/include/tone_map_aces.glsl; both are cross-checked
// against the manifest by tests/shader_abi_parity_tests.cpp, so editing one
// language alone fails the [shader_abi] test.
//
// Include from a product .metal source with:
//     #include "tone_map_aces.metal"
// The product's CMake passes -I <draxul>/shaders/include to `xcrun metal`.
// This file is a header: nothing globs it as a compilation unit.

#ifndef DRAXUL_TONE_MAP_ACES_METAL
#define DRAXUL_TONE_MAP_ACES_METAL

static inline float3 tone_map_aces(float3 hdr, float exposure, float white_point)
{
    float3 color = max(hdr, float3(0.0f)) * max(exposure, 0.0f);
    color /= max(white_point, 1e-3f);
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e),
        float3(0.0f), float3(1.0f));
}

#endif // DRAXUL_TONE_MAP_ACES_METAL
