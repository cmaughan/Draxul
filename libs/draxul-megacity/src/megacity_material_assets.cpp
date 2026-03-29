#include "megacity_material_assets.h"

#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/runtime_path.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace draxul
{

namespace
{

LoadedTextureImage load_rgba8_image(const std::filesystem::path& path)
{
    PERF_MEASURE();
    LoadedTextureImage image;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!pixels)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer,
            "MegaCity: failed to load texture '%s': %s",
            path.string().c_str(),
            stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        return image;
    }

    image.width = width;
    image.height = height;
    image.rgba.assign(pixels, pixels + static_cast<size_t>(width * height * 4));
    stbi_image_free(pixels);
    return image;
}

LoadedTextureImage make_solid_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    LoadedTextureImage image;
    image.width = 1;
    image.height = 1;
    image.rgba = { r, g, b, a };
    return image;
}

} // namespace

std::filesystem::path resolve_megacity_asset_path(const std::filesystem::path& relative_path)
{
    PERF_MEASURE();
    const auto bundled = bundled_asset_path(std::filesystem::path("assets/megacity") / relative_path);
    if (std::filesystem::exists(bundled))
        return bundled;

#ifdef DRAXUL_REPO_ROOT
    const auto repo_path = std::filesystem::path(DRAXUL_REPO_ROOT) / "assets" / "megacity" / relative_path;
    if (std::filesystem::exists(repo_path))
        return repo_path;
#endif

    return bundled;
}

AsphaltRoadMaterialImages load_asphalt_road_material_images()
{
    PERF_MEASURE();
    AsphaltRoadMaterialImages images;
    images.albedo = load_rgba8_image(resolve_megacity_asset_path("textures/Asphalt023S_1K-JPG_Color.jpg"));
    images.normal = load_rgba8_image(resolve_megacity_asset_path("textures/Asphalt023S_1K-JPG_NormalGL.jpg"));
    images.roughness = load_rgba8_image(resolve_megacity_asset_path("textures/Asphalt023S_1K-JPG_Roughness.jpg"));
    images.ao = load_rgba8_image(resolve_megacity_asset_path("textures/Asphalt023S_1K-JPG_AmbientOcclusion.jpg"));
    return images;
}

PavingSidewalkMaterialImages load_paving_sidewalk_material_images()
{
    PERF_MEASURE();
    PavingSidewalkMaterialImages images;
    images.albedo = load_rgba8_image(resolve_megacity_asset_path("textures/PavingStones111_1K-JPG_Color.jpg"));
    images.normal = load_rgba8_image(resolve_megacity_asset_path("textures/PavingStones111_1K-JPG_NormalGL.jpg"));
    images.roughness = load_rgba8_image(resolve_megacity_asset_path("textures/PavingStones111_1K-JPG_Roughness.jpg"));
    images.ao = load_rgba8_image(resolve_megacity_asset_path("textures/PavingStones111_1K-JPG_AmbientOcclusion.jpg"));
    return images;
}

WoodBuildingMaterialImages load_wood_building_material_images()
{
    PERF_MEASURE();
    WoodBuildingMaterialImages images;
    images.albedo = load_rgba8_image(resolve_megacity_asset_path("textures/Bricks060_1K-JPG_Color.jpg"));
    images.normal = load_rgba8_image(resolve_megacity_asset_path("textures/Bricks060_1K-JPG_NormalGL.jpg"));
    images.roughness = load_rgba8_image(resolve_megacity_asset_path("textures/Bricks060_1K-JPG_Roughness.jpg"));
    images.ao = load_rgba8_image(resolve_megacity_asset_path("textures/Bricks060_1K-JPG_AmbientOcclusion.jpg"));
    images.metalness = make_solid_rgba8(0, 0, 0, 255);
    return images;
}

BarkTreeMaterialImages load_bark_tree_material_images()
{
    PERF_MEASURE();
    BarkTreeMaterialImages images;
    images.albedo = load_rgba8_image(resolve_megacity_asset_path("textures/Bark014_1K-JPG_Color.jpg"));
    images.normal = load_rgba8_image(resolve_megacity_asset_path("textures/Bark014_1K-JPG_NormalGL.jpg"));
    images.roughness = load_rgba8_image(resolve_megacity_asset_path("textures/Bark014_1K-JPG_Roughness.jpg"));
    images.ao = load_rgba8_image(resolve_megacity_asset_path("textures/Bark014_1K-JPG_AmbientOcclusion.jpg"));
    return images;
}

LeafAtlasMaterialImages load_leaf_atlas_material_images()
{
    PERF_MEASURE();
    LeafAtlasMaterialImages images;
    images.albedo = load_rgba8_image(resolve_megacity_asset_path("textures/LeafSet023_1K-JPG_Color.jpg"));
    images.normal = load_rgba8_image(resolve_megacity_asset_path("textures/LeafSet023_1K-JPG_NormalGL.jpg"));
    images.roughness = load_rgba8_image(resolve_megacity_asset_path("textures/LeafSet023_1K-JPG_Roughness.jpg"));
    images.opacity = load_rgba8_image(resolve_megacity_asset_path("textures/LeafSet023_1K-JPG_Opacity.jpg"));
    images.scattering = load_rgba8_image(resolve_megacity_asset_path("textures/LeafSet023_1K-JPG_Scattering.jpg"));
    return images;
}

} // namespace draxul
