#include "codeviz_material_library.h"

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
            "CodeViz: failed to load texture '%s': %s",
            path.string().c_str(),
            stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        return image;
    }

    image.width = width;
    image.height = height;
    image.rgba.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
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

std::filesystem::path resolve_codeviz_material_asset_path(const std::filesystem::path& relative_path)
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

namespace
{

TexturedMaterialImages load_textured_material0_images()
{
    PERF_MEASURE();
    TexturedMaterialImages images;
    images.albedo = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Asphalt023S_1K-JPG_Color.jpg"));
    images.normal = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Asphalt023S_1K-JPG_NormalGL.jpg"));
    images.roughness = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Asphalt023S_1K-JPG_Roughness.jpg"));
    images.ao = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Asphalt023S_1K-JPG_AmbientOcclusion.jpg"));
    return images;
}

TexturedMaterialImages load_textured_material1_images()
{
    PERF_MEASURE();
    TexturedMaterialImages images;
    images.albedo = load_rgba8_image(resolve_codeviz_material_asset_path("textures/PavingStones111_1K-JPG_Color.jpg"));
    images.normal = load_rgba8_image(resolve_codeviz_material_asset_path("textures/PavingStones111_1K-JPG_NormalGL.jpg"));
    images.roughness = load_rgba8_image(resolve_codeviz_material_asset_path("textures/PavingStones111_1K-JPG_Roughness.jpg"));
    images.ao = load_rgba8_image(resolve_codeviz_material_asset_path("textures/PavingStones111_1K-JPG_AmbientOcclusion.jpg"));
    return images;
}

VertexTintPbrMaterialImages load_vertex_tint_material0_images()
{
    PERF_MEASURE();
    VertexTintPbrMaterialImages images;
    images.albedo = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Bricks060_1K-JPG_Color.jpg"));
    images.normal = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Bricks060_1K-JPG_NormalGL.jpg"));
    images.roughness = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Bricks060_1K-JPG_Roughness.jpg"));
    images.ao = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Bricks060_1K-JPG_AmbientOcclusion.jpg"));
    images.metalness = make_solid_rgba8(0, 0, 0, 255);
    return images;
}

TexturedMaterialImages load_textured_material2_images()
{
    PERF_MEASURE();
    TexturedMaterialImages images;
    images.albedo = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Bark014_1K-JPG_Color.jpg"));
    images.normal = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Bark014_1K-JPG_NormalGL.jpg"));
    images.roughness = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Bark014_1K-JPG_Roughness.jpg"));
    images.ao = load_rgba8_image(resolve_codeviz_material_asset_path("textures/Bark014_1K-JPG_AmbientOcclusion.jpg"));
    return images;
}

AlphaMaskedPbrMaterialImages load_alpha_masked_material0_images()
{
    PERF_MEASURE();
    AlphaMaskedPbrMaterialImages images;
    images.albedo = load_rgba8_image(resolve_codeviz_material_asset_path("textures/LeafSet023_1K-JPG_Color.jpg"));
    images.normal = load_rgba8_image(resolve_codeviz_material_asset_path("textures/LeafSet023_1K-JPG_NormalGL.jpg"));
    images.roughness = load_rgba8_image(resolve_codeviz_material_asset_path("textures/LeafSet023_1K-JPG_Roughness.jpg"));
    images.opacity = load_rgba8_image(resolve_codeviz_material_asset_path("textures/LeafSet023_1K-JPG_Opacity.jpg"));
    images.scattering = load_rgba8_image(resolve_codeviz_material_asset_path("textures/LeafSet023_1K-JPG_Scattering.jpg"));
    return images;
}

} // namespace

CodeVizMaterialLibraryImages load_codeviz_material_library_images()
{
    PERF_MEASURE();
    CodeVizMaterialLibraryImages images;
    images.textured_pbr0 = load_textured_material0_images();
    images.textured_pbr1 = load_textured_material1_images();
    images.vertex_tint_pbr0 = load_vertex_tint_material0_images();
    images.textured_pbr2 = load_textured_material2_images();
    images.alpha_masked_pbr0 = load_alpha_masked_material0_images();
    return images;
}

} // namespace draxul
