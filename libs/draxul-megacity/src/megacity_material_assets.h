#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace draxul
{

struct LoadedTextureImage
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

struct AsphaltRoadMaterialImages
{
    LoadedTextureImage albedo;
    LoadedTextureImage normal;
    LoadedTextureImage roughness;
    LoadedTextureImage ao;

    [[nodiscard]] bool valid() const
    {
        return albedo.valid() && normal.valid() && roughness.valid() && ao.valid();
    }
};

[[nodiscard]] std::filesystem::path resolve_megacity_asset_path(const std::filesystem::path& relative_path);
[[nodiscard]] AsphaltRoadMaterialImages load_asphalt_road_material_images();

} // namespace draxul
