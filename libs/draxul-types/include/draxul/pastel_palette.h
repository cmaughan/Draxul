#pragma once

#include <array>
#include <cstdint>
#include <glm/vec4.hpp>
#include <string_view>

namespace draxul
{

inline constexpr std::array<glm::vec4, 26> kPastelAccentPalette = {
    glm::vec4(0.961f, 0.878f, 0.863f, 1.0f),
    glm::vec4(0.949f, 0.804f, 0.804f, 1.0f),
    glm::vec4(0.957f, 0.761f, 0.906f, 1.0f),
    glm::vec4(0.796f, 0.651f, 0.969f, 1.0f),
    glm::vec4(0.953f, 0.545f, 0.659f, 1.0f),
    glm::vec4(0.922f, 0.627f, 0.675f, 1.0f),
    glm::vec4(0.980f, 0.702f, 0.529f, 1.0f),
    glm::vec4(0.976f, 0.886f, 0.686f, 1.0f),
    glm::vec4(0.651f, 0.890f, 0.631f, 1.0f),
    glm::vec4(0.580f, 0.886f, 0.835f, 1.0f),
    glm::vec4(0.537f, 0.863f, 0.922f, 1.0f),
    glm::vec4(0.455f, 0.780f, 0.925f, 1.0f),
    glm::vec4(0.537f, 0.706f, 0.980f, 1.0f),
    glm::vec4(0.706f, 0.745f, 0.996f, 1.0f),
    glm::vec4(0.855f, 0.733f, 0.502f, 1.0f),
    glm::vec4(0.643f, 0.827f, 0.502f, 1.0f),
    glm::vec4(0.502f, 0.745f, 0.682f, 1.0f),
    glm::vec4(0.749f, 0.565f, 0.827f, 1.0f),
    glm::vec4(0.890f, 0.643f, 0.584f, 1.0f),
    glm::vec4(0.584f, 0.647f, 0.890f, 1.0f),
    glm::vec4(0.827f, 0.827f, 0.584f, 1.0f),
    glm::vec4(0.502f, 0.827f, 0.890f, 1.0f),
    glm::vec4(0.890f, 0.502f, 0.765f, 1.0f),
    glm::vec4(0.765f, 0.890f, 0.502f, 1.0f),
    glm::vec4(0.682f, 0.549f, 0.451f, 1.0f),
    glm::vec4(0.502f, 0.682f, 0.827f, 1.0f),
};

inline std::uint32_t stable_color_hash(std::string_view text)
{
    std::uint32_t hash = 2166136261u;
    for (const unsigned char ch : text)
    {
        hash ^= ch;
        hash *= 16777619u;
    }
    return hash;
}

inline glm::vec4 pastel_color_from_hash(std::uint32_t hash, float alpha = 1.0f)
{
    glm::vec4 color = kPastelAccentPalette[hash % kPastelAccentPalette.size()];
    color.a = alpha;
    return color;
}

inline glm::vec4 stable_pastel_color(std::string_view key, float alpha = 1.0f)
{
    return pastel_color_from_hash(stable_color_hash(key), alpha);
}

} // namespace draxul
