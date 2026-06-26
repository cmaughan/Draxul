#include "satview_texture_assets.h"

#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/runtime_path.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace draxul::satview
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
            "SatView: failed to load texture '%s': %s",
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

std::filesystem::path resolve_satview_asset_path(const std::filesystem::path& relative_path)
{
    PERF_MEASURE();
    const auto bundled = bundled_asset_path(std::filesystem::path("assets/satview") / relative_path);
    if (std::filesystem::exists(bundled))
        return bundled;

#ifdef DRAXUL_REPO_ROOT
    const auto repo_path = std::filesystem::path(DRAXUL_REPO_ROOT) / "assets" / "satview" / relative_path;
    if (std::filesystem::exists(repo_path))
        return repo_path;
#endif

    return bundled;
}

EarthTextureImages load_earth_texture_images()
{
    PERF_MEASURE();
    EarthTextureImages images;
    images.day = load_rgba8_image(resolve_satview_asset_path("textures/earth_day_8k.jpg"));
    images.night = load_rgba8_image(resolve_satview_asset_path("textures/earth_night_8k.jpg"));
    images.clouds = load_rgba8_image(resolve_satview_asset_path("textures/earth_clouds_8k.jpg"));

    if (!images.day.valid())
        images.day = make_solid_rgba8(16, 70, 120, 255);
    if (!images.night.valid())
        images.night = make_solid_rgba8(1, 3, 9, 255);
    if (!images.clouds.valid())
        images.clouds = make_solid_rgba8(0, 0, 0, 255);
    return images;
}

} // namespace draxul::satview
