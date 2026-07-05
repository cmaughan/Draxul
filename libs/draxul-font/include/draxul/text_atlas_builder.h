#pragma once

#include <array>
#include <cstdint>
#include <draxul/text_atlas.h>
#include <span>
#include <string>

namespace draxul
{

class TextService;

enum class TextAtlasHorizontalAlign
{
    Start,
    Center,
};

enum class TextAtlasVerticalAlign
{
    Center,
    Top,
};

struct TextAtlasRequest
{
    std::string key;
    std::string text;
    glm::ivec2 target_pixel_size{ 1 };
    TextAtlasHorizontalAlign horizontal_align = TextAtlasHorizontalAlign::Center;
    TextAtlasVerticalAlign vertical_align = TextAtlasVerticalAlign::Center;
    std::array<uint8_t, 4> color{ 255, 255, 255, 255 };
    int padding = 2;
    int top_padding = 10;
};

[[nodiscard]] TextAtlas build_text_atlas(
    TextService& text_service,
    std::span<const TextAtlasRequest> requests,
    uint64_t revision);

} // namespace draxul
