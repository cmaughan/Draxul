#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace draxul::satview
{

struct LoadedTextureImage
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

struct EarthTextureImages
{
    LoadedTextureImage day;
    LoadedTextureImage night;
    LoadedTextureImage clouds;

    [[nodiscard]] bool valid() const
    {
        return day.valid() && night.valid() && clouds.valid();
    }
};

[[nodiscard]] std::filesystem::path resolve_satview_asset_path(const std::filesystem::path& relative_path);
[[nodiscard]] LoadedTextureImage load_rgba8_image(const std::filesystem::path& path);
[[nodiscard]] EarthTextureImages load_earth_texture_images();
[[nodiscard]] LoadedTextureImage load_moon_texture_image();
[[nodiscard]] LoadedTextureImage load_sun_texture_image();

} // namespace draxul::satview
