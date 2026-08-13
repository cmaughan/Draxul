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

struct AlphaMaskedPbrMaterialImages
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

[[nodiscard]] std::filesystem::path resolve_codeviz_material_asset_path(const std::filesystem::path& relative_path);

struct CodeVizMaterialLibraryImages
{
    TexturedMaterialImages textured_pbr0;
    TexturedMaterialImages textured_pbr1;
    VertexTintPbrMaterialImages vertex_tint_pbr0;
    TexturedMaterialImages textured_pbr2;
    AlphaMaskedPbrMaterialImages alpha_masked_pbr0;

    [[nodiscard]] bool valid() const
    {
        return textured_pbr0.valid() && textured_pbr1.valid()
            && vertex_tint_pbr0.valid() && textured_pbr2.valid()
            && alpha_masked_pbr0.valid();
    }
};

[[nodiscard]] CodeVizMaterialLibraryImages load_codeviz_material_library_images();

} // namespace draxul
