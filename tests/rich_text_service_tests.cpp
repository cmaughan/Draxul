
#include "support/test_support.h"

#include <catch2/catch_all.hpp>

#include <draxul/rich_text_service.h>

#include <algorithm>
#include <filesystem>
#include <optional>

using namespace draxul;

namespace
{

TextServiceConfig rich_text_test_config()
{
    TextServiceConfig config;
    const auto fonts = draxul::tests::project_root() / "fonts";
    config.font_path = (fonts / "JetBrainsMonoNerdFont-Regular.ttf").string();
    config.bold_font_path = (fonts / "JetBrainsMonoNerdFont-Bold.ttf").string();
    config.italic_font_path = (fonts / "JetBrainsMonoNerdFont-Italic.ttf").string();
    config.bold_italic_font_path = (fonts / "JetBrainsMonoNerdFont-BoldItalic.ttf").string();
    return config;
}

std::optional<std::filesystem::path> proportional_font_path()
{
    for (const auto& path : {
             std::filesystem::path("C:/Windows/Fonts/segoeui.ttf"),
             std::filesystem::path("C:/Windows/Fonts/arial.ttf"),
             std::filesystem::path("/System/Library/Fonts/Supplemental/Arial.ttf"),
             std::filesystem::path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
         })
    {
        if (std::filesystem::exists(path))
            return path;
    }
    return std::nullopt;
}

RichTextService make_initialized_service(float base_point_size = 11.0f)
{
    auto config = rich_text_test_config();
    INFO("bundled font exists");
    REQUIRE(std::filesystem::exists(config.font_path));

    RichTextService service;
    INFO("rich text service initializes");
    REQUIRE(service.initialize(config, base_point_size, 96.0f));
    return service;
}

} // namespace

TEST_CASE("RichTextService resolves markdown text at distinct point sizes", "[font][richtext]")
{
    auto service = make_initialized_service();

    const auto small = service.resolve_cluster("A", { 11.0f });
    const auto large = service.resolve_cluster("A", { 28.0f });

    INFO("larger point size should rasterize to a taller atlas region");
    REQUIRE(large.atlas.bitmap_size.y > small.atlas.bitmap_size.y);
    INFO("larger point size should produce taller font metrics");
    REQUIRE(large.metrics.cell_height > small.metrics.cell_height);

    service.shutdown();
}

TEST_CASE("RichTextService metrics clamp requested point sizes", "[font][richtext]")
{
    auto service = make_initialized_service();

    const RichTextStyleKey below_min{ TextService::MIN_POINT_SIZE - 10.0f };
    const RichTextStyleKey at_min{ TextService::MIN_POINT_SIZE };
    const RichTextStyleKey above_max{ TextService::MAX_POINT_SIZE + 10.0f };
    const RichTextStyleKey at_max{ TextService::MAX_POINT_SIZE };

    INFO("below-min metrics should match min-size metrics");
    REQUIRE(service.metrics_for(below_min).cell_height == service.metrics_for(at_min).cell_height);
    INFO("above-max metrics should match max-size metrics");
    REQUIRE(service.metrics_for(above_max).cell_height == service.metrics_for(at_max).cell_height);

    service.shutdown();
}

TEST_CASE("RichTextService IGlyphAtlas resolve uses the base point size", "[font][richtext]")
{
    auto service = make_initialized_service(13.0f);

    const auto via_atlas = service.resolve_cluster("A", false, false);
    const auto via_rich_text = service.resolve_cluster("A", { 13.0f });

    INFO("IGlyphAtlas resolve should rasterize ASCII text");
    REQUIRE(via_atlas.bitmap_size.x > 0);
    REQUIRE(via_atlas.bitmap_size.y > 0);
    INFO("IGlyphAtlas resolve should use the configured base point size");
    REQUIRE(via_atlas.bitmap_size.y == via_rich_text.atlas.bitmap_size.y);

    service.shutdown();
}

TEST_CASE("RichTextService IGlyphAtlas calls stay bound to the base atlas", "[font][richtext]")
{
    auto service = make_initialized_service(13.0f);

    REQUIRE(service.atlas_snapshots().size() == 1);

    const auto base_before = service.atlas_snapshots().front();
    REQUIRE(base_before.data != nullptr);

    const auto bold_region = service.resolve_cluster("B", true, false);
    REQUIRE(bold_region.bitmap_size.x > 0);
    REQUIRE(bold_region.bitmap_size.y > 0);
    REQUIRE(service.atlas_snapshots().size() == 1);

    const auto base_after_bold = service.atlas_snapshots().front();
    REQUIRE(base_after_bold.atlas_id == base_before.atlas_id);
    REQUIRE(service.atlas_data() == base_after_bold.data);

    const auto heading = service.resolve_cluster("H", { 28.0f, true, false });
    REQUIRE(heading.atlas_id != base_after_bold.atlas_id);
    REQUIRE(service.atlas_snapshots().size() == 2);

    const auto base_after_heading = service.atlas_snapshot(base_after_bold.atlas_id);
    REQUIRE(base_after_heading.has_value());
    REQUIRE(service.atlas_data() == base_after_heading->data);
    REQUIRE(service.atlas_width() == base_after_heading->width);
    REQUIRE(service.atlas_height() == base_after_heading->height);

    service.shutdown();
}

TEST_CASE("RichTextService atlas dirty and reset methods report sane state", "[font][richtext]")
{
    auto service = make_initialized_service();

    INFO("a fresh atlas should not have a pending reset");
    REQUIRE(!service.consume_atlas_reset());

    const auto region = service.resolve_cluster("A", false, false);
    REQUIRE(region.bitmap_size.x > 0);
    REQUIRE(region.bitmap_size.y > 0);

    INFO("resolving a new glyph should dirty the atlas");
    REQUIRE(service.atlas_dirty());
    INFO("dirty rect should include non-empty pixels");
    REQUIRE(service.atlas_dirty_rect().size.x > 0);
    REQUIRE(service.atlas_dirty_rect().size.y > 0);

    service.clear_atlas_dirty();
    INFO("clearing dirtiness resets the dirty flag");
    REQUIRE(!service.atlas_dirty());
    INFO("clearing dirtiness resets the dirty rect");
    REQUIRE(service.atlas_dirty_rect().size.x == 0);
    REQUIRE(service.atlas_dirty_rect().size.y == 0);
    INFO("glyph resolve alone should not mark an atlas reset pending");
    REQUIRE(!service.consume_atlas_reset());

    service.shutdown();
}

TEST_CASE("rich text service exposes stable atlas snapshots per style", "[font][richtext]")
{
    auto service = make_initialized_service();

    const RichTextStyleKey body_style{ 11.0f };
    const RichTextStyleKey heading_style{ 28.0f, true, false };

    const auto body_a = service.resolve_cluster("A", body_style);
    const auto body_b = service.resolve_cluster("B", body_style);
    const auto heading = service.resolve_cluster("H", heading_style);

    INFO("clusters for the same style should share an atlas id");
    REQUIRE(body_a.atlas_id == body_b.atlas_id);
    INFO("distinct styles should use distinct atlases");
    REQUIRE(heading.atlas_id != body_a.atlas_id);
    INFO("resolved clusters should report a nonzero atlas generation");
    REQUIRE(body_a.atlas_generation != 0);
    REQUIRE(heading.atlas_generation != 0);

    const auto snapshots = service.atlas_snapshots();
    REQUIRE(snapshots.size() >= 2);
    REQUIRE(std::find_if(snapshots.begin(), snapshots.end(), [&](const RichTextAtlasSnapshot& snapshot) {
        return snapshot.atlas_id == body_a.atlas_id;
    }) != snapshots.end());
    REQUIRE(std::find_if(snapshots.begin(), snapshots.end(), [&](const RichTextAtlasSnapshot& snapshot) {
        return snapshot.atlas_id == heading.atlas_id;
    }) != snapshots.end());

    const auto body_snapshot = service.atlas_snapshot(body_a.atlas_id);
    const auto heading_snapshot = service.atlas_snapshot(heading.atlas_id);
    REQUIRE(body_snapshot.has_value());
    REQUIRE(heading_snapshot.has_value());

    INFO("body atlas snapshot exposes usable atlas data");
    REQUIRE(body_snapshot->atlas_id == body_a.atlas_id);
    REQUIRE(body_snapshot->generation == body_a.atlas_generation);
    REQUIRE(body_snapshot->data != nullptr);
    REQUIRE(body_snapshot->width > 0);
    REQUIRE(body_snapshot->height > 0);
    REQUIRE(body_snapshot->dirty);
    REQUIRE(body_snapshot->dirty_rect.size.x > 0);
    REQUIRE(body_snapshot->dirty_rect.size.y > 0);

    INFO("heading atlas snapshot exposes usable atlas data");
    REQUIRE(heading_snapshot->atlas_id == heading.atlas_id);
    REQUIRE(heading_snapshot->generation == heading.atlas_generation);
    REQUIRE(heading_snapshot->data != nullptr);
    REQUIRE(heading_snapshot->width > 0);
    REQUIRE(heading_snapshot->height > 0);
    REQUIRE(heading_snapshot->dirty);
    REQUIRE(heading_snapshot->dirty_rect.size.x > 0);
    REQUIRE(heading_snapshot->dirty_rect.size.y > 0);

    service.clear_atlas_dirty(body_a.atlas_id);
    const auto clean_body_snapshot = service.atlas_snapshot(body_a.atlas_id);
    REQUIRE(clean_body_snapshot.has_value());
    REQUIRE(!clean_body_snapshot->dirty);
    REQUIRE(clean_body_snapshot->dirty_rect.size.x == 0);
    REQUIRE(clean_body_snapshot->dirty_rect.size.y == 0);

    service.shutdown();
}

TEST_CASE("RichTextService preserves shaped advance for whitespace-only clusters",
    "[font][richtext]")
{
    auto service = make_initialized_service(13.0f);

    const RichTextStyleKey style{ 13.0f };
    const auto metrics = service.metrics_for(style);
    const auto spaces = service.resolve_cluster("    ", style);

    REQUIRE(spaces.atlas.bitmap_size.x == 0);
    REQUIRE(spaces.atlas.bitmap_size.y == 0);
    REQUIRE(spaces.advance_px >= static_cast<float>(metrics.cell_width * 4));

    service.shutdown();
}

TEST_CASE("RichTextService advance does not include transparent left bearing padding",
    "[font][richtext]")
{
    auto service = make_initialized_service(28.0f);

    const RichTextStyleKey italic_style{ 28.0f, false, true };
    const auto italic = service.resolve_cluster("j", italic_style);

    REQUIRE(italic.atlas.bitmap_size.x > 0);
    if (italic.atlas.bitmap_bearing.x < 0)
    {
        REQUIRE(italic.advance_px <= static_cast<float>(italic.atlas.bitmap_size.x + italic.atlas.bitmap_bearing.x));
    }

    service.shutdown();
}

TEST_CASE("RichTextService uses additive natural advances for markdown text chunks",
    "[font][richtext]")
{
    auto font_path = proportional_font_path();
    if (!font_path.has_value())
        SKIP("No proportional system font available for markdown advance test");

    TextServiceConfig config;
    config.font_path = font_path->string();

    RichTextService service;
    REQUIRE(service.initialize(config, 13.0f, 96.0f));

    const RichTextStyleKey style{ 13.0f };
    const auto full = service.resolve_cluster("Quick reference", style);
    const auto prefix = service.resolve_cluster("Quick re", style);
    const auto suffix = service.resolve_cluster("ference", style);
    const auto narrow = service.resolve_cluster("iiiiii", style);

    REQUIRE(prefix.advance_px + suffix.advance_px == Catch::Approx(full.advance_px).margin(1.0f));
    REQUIRE(narrow.advance_px < static_cast<float>(service.metrics_for(style).cell_width) * 0.85f);

    service.shutdown();
}
