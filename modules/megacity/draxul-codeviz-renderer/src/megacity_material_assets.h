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
            && rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    }
};

struct TexturedMaterialImages
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

using AsphaltRoadMaterialImages = TexturedMaterialImages;
using PavingSidewalkMaterialImages = TexturedMaterialImages;
using BarkTreeMaterialImages = TexturedMaterialImages;

struct VertexTintPbrMaterialImages
{
    LoadedTextureImage albedo;
    LoadedTextureImage normal;
    LoadedTextureImage roughness;
    LoadedTextureImage ao;
    LoadedTextureImage metalness;

    [[nodiscard]] bool valid() const
    {
        return albedo.valid() && normal.valid() && roughness.valid()
            && ao.valid() && metalness.valid();
    }
};

using WoodBuildingMaterialImages = VertexTintPbrMaterialImages;

struct LeafAtlasMaterialImages
{
    LoadedTextureImage albedo;
    LoadedTextureImage normal;
    LoadedTextureImage roughness;
    LoadedTextureImage opacity;
    LoadedTextureImage scattering;

    [[nodiscard]] bool valid() const
    {
        return albedo.valid() && normal.valid() && roughness.valid()
            && opacity.valid() && scattering.valid();
    }
};

[[nodiscard]] std::filesystem::path resolve_megacity_asset_path(const std::filesystem::path& relative_path);
[[nodiscard]] AsphaltRoadMaterialImages load_asphalt_road_material_images();
[[nodiscard]] PavingSidewalkMaterialImages load_paving_sidewalk_material_images();
[[nodiscard]] WoodBuildingMaterialImages load_wood_building_material_images();
[[nodiscard]] BarkTreeMaterialImages load_bark_tree_material_images();
[[nodiscard]] LeafAtlasMaterialImages load_leaf_atlas_material_images();

} // namespace draxul
