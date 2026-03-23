#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include <glm/glm.hpp>

namespace draxul
{

using Color = glm::vec4;

inline Color color_from_rgb(uint32_t rgb)
{
    return Color(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        1.0f);
}

inline Color color_from_rgba(uint32_t rgba)
{
    return Color(
        ((rgba >> 24) & 0xFF) / 255.0f,
        ((rgba >> 16) & 0xFF) / 255.0f,
        ((rgba >> 8) & 0xFF) / 255.0f,
        (rgba & 0xFF) / 255.0f);
}

struct AtlasRegion
{
    glm::vec4 uv = {}; // UV coordinates in atlas (x=u0, y=v0, z=u1, w=v1)
    glm::ivec2 bearing = {}; // Glyph bearing from baseline
    glm::ivec2 size = {}; // Pixel dimensions
    bool is_color = false;
};

enum class AmbiWidth
{
    Single,
    Double
};

struct UiOptions
{
    AmbiWidth ambiwidth = AmbiWidth::Single;
};

enum class CursorShape
{
    Block,
    Horizontal,
    Vertical
};

struct CursorStyle
{
    CursorShape shape = CursorShape::Block;
    Color fg = Color(0.0f, 0.0f, 0.0f, 1.0f);
    Color bg = Color(1.0f, 1.0f, 1.0f, 1.0f);
    int cell_percentage = 0;
    bool use_explicit_colors = false;
};

inline constexpr int kAtlasSize = 2048;

inline constexpr uint32_t STYLE_FLAG_BOLD = 1u << 0;
inline constexpr uint32_t STYLE_FLAG_ITALIC = 1u << 1;
inline constexpr uint32_t STYLE_FLAG_UNDERLINE = 1u << 2;
inline constexpr uint32_t STYLE_FLAG_STRIKETHROUGH = 1u << 3;
inline constexpr uint32_t STYLE_FLAG_UNDERCURL = 1u << 4;
inline constexpr uint32_t STYLE_FLAG_COLOR_GLYPH = 1u << 5;

struct CellUpdate
{
    int col, row;
    Color bg, fg;
    Color sp;
    AtlasRegion glyph;
    uint32_t style_flags = 0;
};

struct CapturedFrame
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;

    bool valid() const
    {
        return width > 0 && height > 0 && rgba.size() == static_cast<size_t>(width * height * 4);
    }
};

} // namespace draxul
