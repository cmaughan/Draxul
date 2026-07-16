#include <catch2/catch_all.hpp>

#include "../libs/draxul-font/src/font_engine.h"
#include "../libs/draxul-font/src/font_resolver.h"
#include "../libs/draxul-font/src/font_selector.h"
#include "../libs/draxul-font/src/glyph_atlas_manager.h"

#include <draxul/text_service.h>

#include <filesystem>

using namespace draxul;

namespace
{

std::filesystem::path test_font_path()
{
    return std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "CascadiaCode-Regular.ttf";
}

struct FontFixture
{
    FontFixture()
    {
        REQUIRE(font.initialize(test_font_path().string(), 13.0f, 96.0f));
        shaper.initialize(font.hb_font());
    }

    ~FontFixture()
    {
        shaper.shutdown();
        font.shutdown();
    }

    FontManager font;
    TextShaper shaper;
};

FT_Error fail_load(FT_Face, FT_UInt, FT_Int32)
{
    return 1;
}

FT_Error fail_render(FT_GlyphSlot, FT_Render_Mode)
{
    return 2;
}

bool fail_convert(const FT_Bitmap&, std::vector<uint8_t>&, bool&)
{
    return false;
}

bool fail_reserve(int, int)
{
    return false;
}

FT_Face failing_face = nullptr;

FT_Error fail_one_face_load(FT_Face face, FT_UInt glyph, FT_Int32 flags)
{
    return face == failing_face ? 3 : FT_Load_Glyph(face, glyph, flags);
}

} // namespace

TEST_CASE("glyph raster errors distinguish font failures from atlas capacity", "[font]")
{
    if (!std::filesystem::exists(test_font_path()))
        SKIP("test font not found");

    FontFixture fixture;
    GlyphCache cache;
    REQUIRE(cache.initialize(fixture.font.face(), 13, 2048));

    SECTION("font load failure")
    {
        cache.set_raster_operations_for_testing({ .load_glyph = &fail_load });
        const auto result = cache.get_cluster_result(std::string(4096, 'A'), fixture.font.face(), fixture.shaper);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == ErrorKind::FontGlyphLoadFailed);
        CHECK(result.error().message.size() < 256);
        CHECK_FALSE(cache.consume_overflowed());
        CHECK(cache.glyph_count() == 0);
    }

    SECTION("glyph render failure")
    {
        cache.set_raster_operations_for_testing({ .render_glyph = &fail_render });
        const auto result = cache.get_cluster_result("A", fixture.font.face(), fixture.shaper);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == ErrorKind::GlyphRasterizationFailed);
        CHECK_FALSE(cache.consume_overflowed());
        CHECK(cache.glyph_count() == 0);
    }

    SECTION("bitmap conversion failure")
    {
        cache.set_raster_operations_for_testing({ .convert_bitmap = &fail_convert });
        const auto result = cache.get_cluster_result("A", fixture.font.face(), fixture.shaper);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == ErrorKind::InvalidGlyphBitmap);
        CHECK_FALSE(cache.consume_overflowed());
        CHECK(cache.glyph_count() == 0);
    }

    SECTION("atlas reserve failure")
    {
        cache.set_raster_operations_for_testing({ .reserve_region = &fail_reserve });
        const auto result = cache.get_cluster_result("A", fixture.font.face(), fixture.shaper);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == ErrorKind::AtlasOverflow);
        CHECK(cache.consume_overflowed());
        CHECK(cache.glyph_count() == 0);
    }
}

TEST_CASE("glyph raster failure falls through to a valid fallback without resetting atlas", "[font]")
{
    if (!std::filesystem::exists(test_font_path()))
        SKIP("test font not found");

    TextServiceConfig config;
    config.font_path = test_font_path().string();

    FontResolver resolver;
    REQUIRE(resolver.initialize(config, 13.0f, 96.0f));
    if (resolver.fallbacks().empty())
    {
        resolver.shutdown();
        SKIP("no platform fallback font is available");
    }
    bool renderable_fallback = false;
    for (size_t i = 0; i < resolver.fallbacks().size(); ++i)
    {
        if (resolver.ensure_loaded(i)
            && detail::can_render_cluster(
                resolver.fallbacks()[i].font.face(), resolver.fallbacks()[i].shaper, "A"))
        {
            renderable_fallback = true;
            break;
        }
    }
    if (!renderable_fallback)
    {
        resolver.shutdown();
        SKIP("no platform fallback font can render the test cluster");
    }

    FontSelector selector;
    GlyphAtlasManager manager;
    REQUIRE(manager.initialize(resolver.primary().face(), 13));

    failing_face = resolver.primary().face();
    manager.cache().set_raster_operations_for_testing({ .load_glyph = &fail_one_face_load });
    const AtlasRegion region = manager.resolve_cluster("A", selector, resolver);
    failing_face = nullptr;

    CHECK((region.bitmap_size.x > 0 || region.bitmap_size.y > 0));
    CHECK(manager.reset_count() == 0);
    CHECK_FALSE(manager.consume_atlas_reset());
    resolver.shutdown();
}

TEST_CASE("glyph atlas manager resets only for capacity and rate-limits exhausted warnings", "[font]")
{
    if (!std::filesystem::exists(test_font_path()))
        SKIP("test font not found");

    TextServiceConfig config;
    config.font_path = test_font_path().string();
    FontResolver resolver;
    REQUIRE(resolver.initialize(config, 13.0f, 96.0f));

    SECTION("font failure does not reset and produces one consumable warning")
    {
        FontSelector selector;
        GlyphAtlasManager manager;
        REQUIRE(manager.initialize(resolver.primary().face(), 13));
        manager.cache().set_raster_operations_for_testing({ .load_glyph = &fail_load });

        CHECK(manager.resolve_cluster("A", selector, resolver).bitmap_size.x == 0);
        CHECK(manager.reset_count() == 0);
        auto warnings = manager.take_runtime_warnings();
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].find("FT_Load_Glyph") != std::string::npos);

        CHECK(manager.resolve_cluster("B", selector, resolver).bitmap_size.x == 0);
        CHECK(manager.take_runtime_warnings().empty());
        CHECK(manager.reset_count() == 0);
    }

    SECTION("capacity failure resets and retries exactly once")
    {
        FontSelector selector;
        GlyphAtlasManager manager;
        REQUIRE(manager.initialize(resolver.primary().face(), 13));
        manager.cache().set_raster_operations_for_testing({ .reserve_region = &fail_reserve });

        CHECK(manager.resolve_cluster("A", selector, resolver).bitmap_size.x == 0);
        CHECK(manager.reset_count() == 1);
        CHECK(manager.consume_atlas_reset());
    }

    resolver.shutdown();
}
