#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace draxul
{

class TextService;

struct BuildingTooltipData
{
    std::string name;
    std::string module_path;
    int function_count = 0;
    int field_count = 0;
};

struct TooltipBitmap
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;

    [[nodiscard]] bool valid() const
    {
        return width > 0 && height > 0
            && rgba.size() == static_cast<size_t>(width * height * 4);
    }
};

/// Rasterize a multi-line tooltip bitmap with a semi-transparent dark background
/// and light text showing the building's metadata.
[[nodiscard]] TooltipBitmap rasterize_tooltip(
    TextService& text_service, const BuildingTooltipData& data);

} // namespace draxul
