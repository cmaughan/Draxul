#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace draxul
{

struct TextAtlasImage
{
    int width = 0;
    int height = 0;
    uint64_t revision = 0;
    std::vector<uint8_t> rgba;

    [[nodiscard]] bool valid() const
    {
        return width > 0 && height > 0
            && rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    }
};

struct TextAtlasEntry
{
    glm::vec4 uv_rect{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::ivec2 pixel_size{ 0 };
    glm::ivec2 ink_pixel_size{ 0 };
};

struct TextAtlas
{
    TextAtlasImage image;
    std::unordered_map<std::string, TextAtlasEntry> entries;
};

} // namespace draxul
