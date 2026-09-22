#include "../libs/draxul-nanovg/backend/src/nanovg_paint.h"
#if defined(__APPLE__)
#include "../libs/draxul-nanovg/backend/src/nanovg_mtl_test_hooks.h"
#endif

#include <nanovg.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace
{

using Catch::Approx;
using draxul::nanovg_detail::PaintConversionResult;
using draxul::nanovg_detail::PaintShaderType;
using draxul::nanovg_detail::PaintUniforms;
using draxul::nanovg_detail::TextureMetadata;

NVGpaint gradient_paint()
{
    NVGpaint paint{};
    nvgTransformIdentity(paint.xform);
    paint.extent[0] = 24.0f;
    paint.extent[1] = 36.0f;
    paint.radius = 7.0f;
    paint.feather = 3.5f;
    paint.innerColor = nvgRGBAf(0.8f, 0.4f, 0.2f, 0.5f);
    paint.outerColor = nvgRGBAf(0.25f, 0.5f, 1.0f, 0.25f);
    return paint;
}

NVGscissor disabled_scissor()
{
    NVGscissor scissor{};
    nvgTransformIdentity(scissor.xform);
    scissor.extent[0] = -1.0f;
    scissor.extent[1] = -1.0f;
    return scissor;
}

bool all_zero(const std::uint8_t* begin, const std::uint8_t* end)
{
    return std::all_of(begin, end, [](std::uint8_t value) { return value == 0; });
}

} // namespace

TEST_CASE("NanoVG paint converts gradients into the native uniform ABI",
    "[nanovg][paint][cpu]")
{
    auto paint = gradient_paint();
    auto scissor = disabled_scissor();
    PaintUniforms output;
    std::memset(&output, 0xff, sizeof(output));

    const auto result = draxul::nanovg_detail::convert_paint(
        output, paint, scissor, 5.0f, 2.0f, 0.125f, std::nullopt);

    CHECK(result == PaintConversionResult::Complete);
    CHECK(output.type == static_cast<int>(PaintShaderType::FillGradient));
    CHECK(output.innerCol[0] == Approx(0.4f));
    CHECK(output.innerCol[1] == Approx(0.2f));
    CHECK(output.innerCol[2] == Approx(0.1f));
    CHECK(output.innerCol[3] == Approx(0.5f));
    CHECK(output.outerCol[0] == Approx(0.0625f));
    CHECK(output.outerCol[1] == Approx(0.125f));
    CHECK(output.outerCol[2] == Approx(0.25f));
    CHECK(output.outerCol[3] == Approx(0.25f));
    CHECK(output.scissorExt[0] == Approx(1.0f));
    CHECK(output.scissorExt[1] == Approx(1.0f));
    CHECK(output.scissorScale[0] == Approx(1.0f));
    CHECK(output.scissorScale[1] == Approx(1.0f));
    CHECK(output.extent[0] == Approx(24.0f));
    CHECK(output.extent[1] == Approx(36.0f));
    CHECK(output.radius == Approx(7.0f));
    CHECK(output.feather == Approx(3.5f));
    CHECK(output.strokeMult == Approx(1.75f));
    CHECK(output.strokeThr == Approx(0.125f));
    CHECK(output.paintMat[0] == Approx(1.0f));
    CHECK(output.paintMat[5] == Approx(1.0f));
    CHECK(output.paintMat[10] == Approx(1.0f));
    CHECK(all_zero(output.padding, output.padding + sizeof(output.padding)));
}

TEST_CASE("NanoVG paint packs transformed scissors and matrices",
    "[nanovg][paint][cpu]")
{
    auto paint = gradient_paint();
    const float paint_transform[6] = { 2.0f, 0.0f, 0.0f, 4.0f, 10.0f, 20.0f };
    std::copy(std::begin(paint_transform), std::end(paint_transform), paint.xform);
    NVGscissor scissor{};
    const float scissor_transform[6]
        = { 3.0f, 0.0f, 0.0f, 4.0f, 9.0f, 8.0f };
    std::copy(std::begin(scissor_transform), std::end(scissor_transform),
        scissor.xform);
    scissor.extent[0] = 11.0f;
    scissor.extent[1] = 12.0f;
    PaintUniforms output{};

    REQUIRE(draxul::nanovg_detail::convert_paint(
                output, paint, scissor, 3.0f, 2.0f, -1.0f, std::nullopt)
        == PaintConversionResult::Complete);

    CHECK(output.scissorMat[0] == Approx(1.0f / 3.0f));
    CHECK(output.scissorMat[5] == Approx(0.25f));
    CHECK(output.scissorMat[8] == Approx(-3.0f));
    CHECK(output.scissorMat[9] == Approx(-2.0f));
    CHECK(output.scissorScale[0] == Approx(1.5f));
    CHECK(output.scissorScale[1] == Approx(2.0f));
    CHECK(output.paintMat[0] == Approx(0.5f));
    CHECK(output.paintMat[5] == Approx(0.25f));
    CHECK(output.paintMat[8] == Approx(-5.0f));
    CHECK(output.paintMat[9] == Approx(-5.0f));
    CHECK(output.paintMat[2] == Approx(0.0f));
    CHECK(output.paintMat[3] == Approx(0.0f));
    CHECK(output.paintMat[10] == Approx(1.0f));
    CHECK(output.paintMat[11] == Approx(0.0f));
}

TEST_CASE("NanoVG paint classifies RGBA and alpha image textures",
    "[nanovg][paint][cpu]")
{
    auto paint = gradient_paint();
    paint.image = 17;
    auto scissor = disabled_scissor();
    PaintUniforms output{};

    REQUIRE(draxul::nanovg_detail::convert_paint(output, paint, scissor,
                1.0f, 1.0f, 0.0f,
                TextureMetadata{ NVG_TEXTURE_RGBA, NVG_IMAGE_PREMULTIPLIED })
        == PaintConversionResult::Complete);
    CHECK(output.type == static_cast<int>(PaintShaderType::FillImage));
    CHECK(output.texType == 0);

    REQUIRE(draxul::nanovg_detail::convert_paint(output, paint, scissor,
                1.0f, 1.0f, 0.0f,
                TextureMetadata{ NVG_TEXTURE_RGBA, 0 })
        == PaintConversionResult::Complete);
    CHECK(output.texType == 1);

    REQUIRE(draxul::nanovg_detail::convert_paint(output, paint, scissor,
                1.0f, 1.0f, 0.0f,
                TextureMetadata{ NVG_TEXTURE_ALPHA, 0 })
        == PaintConversionResult::Complete);
    CHECK(output.texType == 2);
}

TEST_CASE("NanoVG paint applies image Y flip around the paint extent",
    "[nanovg][paint][cpu]")
{
    auto paint = gradient_paint();
    paint.image = 4;
    nvgTransformTranslate(paint.xform, 7.0f, 13.0f);
    auto scissor = disabled_scissor();
    PaintUniforms normal{};
    PaintUniforms flipped{};

    REQUIRE(draxul::nanovg_detail::convert_paint(normal, paint, scissor,
                1.0f, 1.0f, 0.0f, TextureMetadata{ NVG_TEXTURE_RGBA, 0 })
        == PaintConversionResult::Complete);
    REQUIRE(draxul::nanovg_detail::convert_paint(flipped, paint, scissor,
                1.0f, 1.0f, 0.0f,
                TextureMetadata{ NVG_TEXTURE_RGBA, NVG_IMAGE_FLIPY })
        == PaintConversionResult::Complete);

    CHECK(normal.paintMat[5] == Approx(1.0f));
    CHECK(flipped.paintMat[5] == Approx(-1.0f));
    CHECK(flipped.paintMat[9] != Approx(normal.paintMat[9]));
}

TEST_CASE("NanoVG paint preserves partial output when texture metadata is missing",
    "[nanovg][paint][cpu]")
{
    auto paint = gradient_paint();
    paint.image = 99;
    auto scissor = disabled_scissor();
    PaintUniforms output;
    std::memset(&output, 0xff, sizeof(output));

    const auto result = draxul::nanovg_detail::convert_paint(
        output, paint, scissor, 4.0f, 2.0f, 0.75f, std::nullopt);

    CHECK(result == PaintConversionResult::MissingTextureMetadata);
    CHECK(output.innerCol[0] == Approx(0.4f));
    CHECK(output.scissorExt[0] == Approx(1.0f));
    CHECK(output.extent[0] == Approx(24.0f));
    CHECK(output.strokeMult == Approx(1.5f));
    CHECK(output.strokeThr == Approx(0.75f));
    CHECK(output.type == 0);
    CHECK(output.texType == 0);
    CHECK(std::all_of(std::begin(output.paintMat), std::end(output.paintMat),
        [](float value) { return value == 0.0f; }));
    CHECK(all_zero(output.padding, output.padding + sizeof(output.padding)));
}

#if defined(__APPLE__)
TEST_CASE("NanoVG Metal creation failures delete each backend exactly once",
    "[nanovg][metal][ownership]")
{
    using draxul::nanovg_mtl_test::exercise_creation;
    using draxul::nanovg_mtl_test::FailurePoint;

    SECTION("initial NanoVG context allocation fails before callbacks install")
    {
        const auto result
            = exercise_creation(FailurePoint::InitialContextAllocation);
        CHECK_FALSE(result.context_created);
        CHECK(result.backend_allocations == 1);
        CHECK(result.backend_deletions == 1);
        CHECK(result.render_create_calls == 0);
        CHECK(result.atlas_create_calls == 0);
        CHECK(result.render_delete_calls == 0);
    }

    SECTION("renderer initialization failure uses NanoVG cleanup")
    {
        const auto result
            = exercise_creation(FailurePoint::RendererInitialization);
        CHECK_FALSE(result.context_created);
        CHECK(result.backend_allocations == 1);
        CHECK(result.backend_deletions == 1);
        CHECK(result.render_create_calls == 1);
        CHECK(result.atlas_create_calls == 0);
        CHECK(result.render_delete_calls == 1);
    }

    SECTION("initial font atlas failure uses NanoVG cleanup")
    {
        const auto result = exercise_creation(FailurePoint::AtlasAllocation);
        CHECK_FALSE(result.context_created);
        CHECK(result.backend_allocations == 1);
        CHECK(result.backend_deletions == 1);
        CHECK(result.render_create_calls == 1);
        CHECK(result.atlas_create_calls == 1);
        CHECK(result.render_delete_calls == 1);
    }
}

TEST_CASE("NanoVG Metal normal creation transfers ownership to teardown",
    "[nanovg][metal][ownership]")
{
    const auto result = draxul::nanovg_mtl_test::exercise_creation(
        draxul::nanovg_mtl_test::FailurePoint::None);
    if (!result.device_available)
        SKIP("no Metal device is available");

    REQUIRE(result.context_created);
    CHECK(result.backend_allocations == 1);
    CHECK(result.backend_deletions == 1);
    CHECK(result.render_create_calls == 1);
    CHECK(result.atlas_create_calls == 1);
    CHECK(result.render_delete_calls == 1);
}
#endif
