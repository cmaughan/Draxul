#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace draxul
{
class TextService;
}

namespace draxul::gui
{

struct TooltipEntry
{
    std::string label;
    std::string value;
};

struct TooltipData
{
    std::vector<TooltipEntry> entries;
};

struct TooltipBitmap
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;

    [[nodiscard]] bool valid() const
    {
        return width > 0 && height > 0
            && rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    }
};

/// Rasterize a multi-line tooltip bitmap with a semi-transparent dark background
/// and light text showing a 2-column table of labels and values.
[[nodiscard]] TooltipBitmap rasterize_tooltip(
    draxul::TextService& text_service, const TooltipData& data);

} // namespace draxul::gui
