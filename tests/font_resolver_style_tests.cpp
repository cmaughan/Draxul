#include "support/test_support.h"

#include <catch2/catch_all.hpp>

#include <draxul/text_service.h>

#include "font_resolver.h"
#include "font_selector.h"
#include "font_style.h"

#include <filesystem>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace draxul;

TEST_CASE("Fluent Emoji renders Kanban icons in color across styles and sizes", "[font][fluent]")
{
    const float ppi = GENERATE(96.0f, 192.0f);
    const float size = GENERATE(11.0f, 16.0f);
    TextServiceConfig config;
    config.font_path = draxul::tests::bundled_font_path().string();
    config.enable_ligatures = GENERATE(true, false);
    TextService service;
    REQUIRE(service.initialize(config, size, ppi));
    FontResolver resolver;
    REQUIRE(resolver.initialize(config, size, ppi));
    REQUIRE_FALSE(resolver.fallbacks().empty());
    REQUIRE(std::filesystem::path(resolver.fallbacks().front().path).filename() == "FluentEmojiFlat.ttf");
    REQUIRE(resolver.ensure_loaded(0));
    FontSelector selector;
    for (const std::string icon : { "🐛", "✨", "🔧", "📈", "🧪", "📄", "🚨", "🔥", "⚡", "🔹", "👍🏽", "👨‍💻" })
    {
        INFO(icon << " at " << size << "pt / " << ppi << "ppi");
        for (const auto style : { FontStyle::Regular, FontStyle::Bold, FontStyle::Italic, FontStyle::BoldItalic })
        {
            const bool bold = style == FontStyle::Bold || style == FontStyle::BoldItalic;
            const bool italic = style == FontStyle::Italic || style == FontStyle::BoldItalic;
            REQUIRE(selector.select(icon, resolver, bold, italic).face == resolver.fallbacks().front().font.face());
            REQUIRE(selector.select(icon, resolver, bold, italic).shaper->shape(icon).size() == 1);
            const auto region = service.resolve_cluster(icon, bold, italic);
            REQUIRE(region.bitmap_size.x > 0);
            REQUIRE(region.bitmap_size.y > 0);
            REQUIRE(region.is_color);
            REQUIRE(region.bitmap_size.y <= service.metrics().cell_height);
            REQUIRE(region.bitmap_size.x <= service.metrics().cell_width * 2);
            REQUIRE(region.advance_px <= service.metrics().cell_width * 2);
            const int x = static_cast<int>(std::lround(region.uv.x * service.atlas_width()));
            const int y = static_cast<int>(std::lround(region.uv.y * service.atlas_height()));
            bool colored_pixel = false;
            for (int row = 0; row < region.bitmap_size.y; ++row)
                for (int col = 0; col < region.bitmap_size.x; ++col)
                {
                    const auto* pixel = service.atlas_data() + ((y + row) * service.atlas_width() + x + col) * 4;
                    colored_pixel |= pixel[3] > 0 && (pixel[0] != pixel[1] || pixel[1] != pixel[2]);
                }
            REQUIRE(colored_pixel);
            REQUIRE(selector.select("A", resolver, bold, italic).face == resolver.style(style).font.face());
            REQUIRE_FALSE(service.resolve_cluster("A", bold, italic).is_color);
        }
    }
    // Zoom recreates faces and atlas entries at the new size.
    REQUIRE(service.set_point_size(size + 2));
    REQUIRE(service.resolve_cluster("🔥", true, false).is_color);
    REQUIRE(service.resolve_cluster("🔥").bitmap_size.y <= service.metrics().cell_height);
}

TEST_CASE("an emoji font must compose the complete joined grapheme", "[font][fluent]")
{
    TextServiceConfig config;
    config.font_path = draxul::tests::bundled_font_path().string();
    config.enable_ligatures = false;
    FontResolver resolver;
    REQUIRE(resolver.initialize(config, 11, 192));
    REQUIRE(resolver.ensure_loaded(0));
    auto& fluent = resolver.fallbacks().front();
    const std::string family = "👨‍👩‍👧‍👦";
    REQUIRE_FALSE(detail::can_render_cluster(fluent.font.face(), fluent.shaper, family));
#if defined(__APPLE__) || defined(_WIN32)
    // Apple Color Emoji composes the family. Segoe UI Emoji stopped composing
    // family sequences in its September 2026 Windows 11 update and shapes the
    // four members instead. Either way the coverage gap must proceed down the
    // fallback chain to the most complete color composition available, never
    // to missing-glyph boxes or Fluent's visible joiner spaces.
    size_t best = 0;
    for (size_t i = 0; i < resolver.fallbacks().size(); ++i)
    {
        if (!resolver.ensure_loaded(i) || !detail::font_has_color(resolver.fallbacks()[i].font.face()))
            continue;
        const size_t glyphs = detail::renderable_glyph_count(
            resolver.fallbacks()[i].font.face(), resolver.fallbacks()[i].shaper, family);
        if (glyphs > 0 && (best == 0 || glyphs < best))
            best = glyphs;
    }
    REQUIRE(best > 0);
#if defined(__APPLE__)
    REQUIRE(best == 1);
#endif
    FontSelector selector;
    const auto selected = selector.select(family, resolver, true, false);
    REQUIRE(selected.face != fluent.font.face());
    REQUIRE(detail::font_has_color(selected.face));
    REQUIRE(detail::renderable_glyph_count(selected.face, *selected.shaper, family) == best);
    TextService service;
    REQUIRE(service.initialize(config, 11, 192));
    const auto region = service.resolve_cluster(family, true, false);
    REQUIRE(region.is_color);
    REQUIRE(region.bitmap_size.x <= service.metrics().cell_width * 2);
#endif
}

TEST_CASE("explicit emoji fallback paths replace the bundled defaults", "[font][fluent]")
{
    TextServiceConfig config;
    config.font_path = draxul::tests::bundled_font_path().string();
    config.fallback_paths = { draxul::tests::bundled_font_path("CascadiaCode-Regular.ttf").string() };
    FontResolver resolver;
    REQUIRE(resolver.initialize(config, 11, 96));
    REQUIRE(resolver.fallbacks().size() == 1);
    REQUIRE(resolver.fallbacks().front().path == config.fallback_paths.front());
}

// Parity table tests for the indexed FontStyle model in FontResolver and
// FontSelector: resolution, fallback precedence, missing-variant
// combinations, config path mapping, and cache/reinitialize invalidation
// (kanban: 30 font-style-model -refactor). These lock in the exact
// pre-refactor behavior for all four styles.

namespace
{

std::filesystem::path jetbrains_font(const char* variant)
{
    return draxul::tests::project_root() / "fonts" / (std::string("JetBrainsMonoNerdFont-") + variant + ".ttf");
}

// CascadiaCode ships without style-variant files, so it acts as the
// "regular-only" primary in missing-variant scenarios.
std::filesystem::path regular_only_font()
{
    return draxul::tests::project_root() / "fonts" / "CascadiaCode-Regular.ttf";
}

} // namespace

TEST_CASE("font resolver auto-detects a variant file for every style", "[font]")
{
    INFO("bundled fonts exist");
    REQUIRE(std::filesystem::exists(jetbrains_font("Regular")));

    FontResolver resolver;
    TextServiceConfig config;
    config.font_path = jetbrains_font("Regular").string();
    INFO("font resolver initializes");
    REQUIRE(resolver.initialize(config, 11, 96.0f));

    struct Row
    {
        FontStyle style;
        const char* expected_file;
    };
    const Row rows[] = {
        { FontStyle::Regular, "JetBrainsMonoNerdFont-Regular.ttf" },
        { FontStyle::Bold, "JetBrainsMonoNerdFont-Bold.ttf" },
        { FontStyle::Italic, "JetBrainsMonoNerdFont-Italic.ttf" },
        { FontStyle::BoldItalic, "JetBrainsMonoNerdFont-BoldItalic.ttf" },
    };

    std::set<FT_Face> faces;
    for (const auto& row : rows)
    {
        INFO(font_style_display_name(row.style));
        REQUIRE(resolver.has_style(row.style));
        REQUIRE(resolver.style(row.style).font.face() != nullptr);
        INFO("slot path points at the auto-detected variant file");
        REQUIRE(std::filesystem::path(resolver.style(row.style).path).filename() == row.expected_file);
        faces.insert(resolver.style(row.style).font.face());
    }
    INFO("each style resolves to a distinct face");
    REQUIRE(faces.size() == FONT_STYLE_COUNT);

    INFO("no warnings when every variant resolves");
    REQUIRE(resolver.take_warnings().empty());

    resolver.shutdown();

    for (FontStyle variant : FONT_STYLE_VARIANTS)
    {
        INFO("shutdown clears every style slot");
        REQUIRE_FALSE(resolver.has_style(variant));
        REQUIRE(resolver.style(variant).path.empty());
    }
}

TEST_CASE("font resolver reports a style-specific warning for each missing variant", "[font]")
{
    INFO("regular-only font exists");
    REQUIRE(std::filesystem::exists(regular_only_font()));

    FontResolver resolver;
    TextServiceConfig config;
    config.font_path = regular_only_font().string();
    INFO("font resolver initializes without variants");
    REQUIRE(resolver.initialize(config, 11, 96.0f));

    INFO("the regular style is always available after initialize");
    REQUIRE(resolver.has_style(FontStyle::Regular));
    for (FontStyle variant : FONT_STYLE_VARIANTS)
    {
        INFO(font_style_display_name(variant));
        REQUIRE_FALSE(resolver.has_style(variant));
        REQUIRE(resolver.style(variant).path.empty());
    }

    const std::vector<std::string> expected = {
        "No bold font variant found; using regular",
        "No italic font variant found; using regular",
        "No bold-italic font variant found; using regular",
    };
    INFO("warning text and order are style-specific");
    REQUIRE(resolver.take_warnings() == expected);

    resolver.shutdown();
}

TEST_CASE("explicit config paths map each style variant to its slot", "[font]")
{
    FontResolver resolver;
    TextServiceConfig config;
    config.font_path = regular_only_font().string();
    config.bold_font_path = jetbrains_font("Bold").string();
    config.italic_font_path = jetbrains_font("Italic").string();
    config.bold_italic_font_path = jetbrains_font("BoldItalic").string();
    INFO("font resolver initializes with explicit variant paths");
    REQUIRE(resolver.initialize(config, 11, 96.0f));

    struct Row
    {
        FontStyle style;
        const std::string* config_path;
    };
    const Row rows[] = {
        { FontStyle::Regular, &config.font_path },
        { FontStyle::Bold, &config.bold_font_path },
        { FontStyle::Italic, &config.italic_font_path },
        { FontStyle::BoldItalic, &config.bold_italic_font_path },
    };
    for (const auto& row : rows)
    {
        INFO(font_style_display_name(row.style));
        REQUIRE(resolver.has_style(row.style));
        INFO("config path round-trips into the style slot");
        REQUIRE(resolver.style(row.style).path == *row.config_path);
    }

    INFO("explicit paths that load produce no warnings");
    REQUIRE(resolver.take_warnings().empty());

    resolver.shutdown();
}

TEST_CASE("unloadable explicit variant path degrades with a not-found warning", "[font]")
{
    FontResolver resolver;
    TextServiceConfig config;
    config.font_path = regular_only_font().string();
    config.bold_font_path = (draxul::tests::project_root() / "fonts" / "DoesNotExist-Bold.ttf").string();
    INFO("font resolver initializes despite the bad variant path");
    REQUIRE(resolver.initialize(config, 11, 96.0f));

    INFO("the bold slot stays unavailable");
    REQUIRE_FALSE(resolver.has_style(FontStyle::Bold));

    const std::vector<std::string> expected = {
        "Bold font not found: " + config.bold_font_path,
        "No italic font variant found; using regular",
        "No bold-italic font variant found; using regular",
    };
    INFO("the bold warning names the configured path");
    REQUIRE(resolver.take_warnings() == expected);

    resolver.shutdown();
}

TEST_CASE("variant path equal to the regular font is skipped without warning", "[font]")
{
    FontResolver resolver;
    TextServiceConfig config;
    config.font_path = regular_only_font().string();
    config.bold_font_path = config.font_path;
    INFO("font resolver initializes");
    REQUIRE(resolver.initialize(config, 11, 96.0f));

    INFO("a variant identical to the regular font is not loaded twice");
    REQUIRE_FALSE(resolver.has_style(FontStyle::Bold));

    const std::vector<std::string> expected = {
        "No italic font variant found; using regular",
        "No bold-italic font variant found; using regular",
    };
    INFO("no bold warning is emitted for the same-as-regular case");
    REQUIRE(resolver.take_warnings() == expected);

    resolver.shutdown();
}

TEST_CASE("style selection degrades through the documented precedence for every availability combination",
    "[font]")
{
    const std::string text = "A";

    // All eight combinations of which variant files are configured.
    for (int mask = 0; mask < 8; ++mask)
    {
        const bool with_bold = (mask & 1) != 0;
        const bool with_italic = (mask & 2) != 0;
        const bool with_bold_italic = (mask & 4) != 0;

        TextServiceConfig config;
        config.font_path = regular_only_font().string();
        if (with_bold)
            config.bold_font_path = jetbrains_font("Bold").string();
        if (with_italic)
            config.italic_font_path = jetbrains_font("Italic").string();
        if (with_bold_italic)
            config.bold_italic_font_path = jetbrains_font("BoldItalic").string();

        FontResolver resolver;
        INFO("bold=" << with_bold << " italic=" << with_italic << " bold-italic=" << with_bold_italic);
        REQUIRE(resolver.initialize(config, 11, 96.0f));
        FontSelector selector;

        const auto face_of = [&resolver](FontStyle style) { return resolver.style(style).font.face(); };

        struct Request
        {
            bool is_bold;
            bool is_italic;
        };
        const Request requests[] = { { false, false }, { true, false }, { false, true }, { true, true } };
        for (const auto& request : requests)
        {
            // Expected precedence, restated literally from the pre-refactor
            // branch chain: exact variant first; bold-italic degrades to
            // bold, then italic; bold/italic degrade straight to regular.
            FT_Face expected = face_of(FontStyle::Regular);
            if (request.is_bold && request.is_italic)
            {
                if (with_bold_italic)
                    expected = face_of(FontStyle::BoldItalic);
                else if (with_bold)
                    expected = face_of(FontStyle::Bold);
                else if (with_italic)
                    expected = face_of(FontStyle::Italic);
            }
            else if (request.is_bold && with_bold)
            {
                expected = face_of(FontStyle::Bold);
            }
            else if (request.is_italic && with_italic)
            {
                expected = face_of(FontStyle::Italic);
            }

            const auto selection = selector.select(text, resolver, request.is_bold, request.is_italic);
            INFO("request bold=" << request.is_bold << " italic=" << request.is_italic);
            REQUIRE(selection.face == expected);
            REQUIRE(selection.shaper != nullptr);
        }

        resolver.shutdown();
    }
}

TEST_CASE("set_point_size resizes every loaded style variant", "[font]")
{
    FontResolver resolver;
    TextServiceConfig config;
    config.font_path = jetbrains_font("Regular").string();
    INFO("font resolver initializes with all variants");
    REQUIRE(resolver.initialize(config, 11, 96.0f));
    INFO("point size change succeeds");
    REQUIRE(resolver.set_point_size(14.0f));

    for (size_t i = 0; i < FONT_STYLE_COUNT; ++i)
    {
        const auto style = static_cast<FontStyle>(i);
        INFO(font_style_display_name(style));
        REQUIRE(resolver.has_style(style));
        REQUIRE(resolver.style(style).font.point_size() == 14.0f);
        INFO("shapers stay attached to the resized face");
        REQUIRE_FALSE(resolver.style(style).shaper.shape("A").empty());
        REQUIRE_FALSE(resolver.style(style).unligated_shaper.shape("A").empty());
    }

    resolver.shutdown();
}

TEST_CASE("selector rebinds style selections after resolver reinitialize", "[font]")
{
    FontResolver resolver;
    TextServiceConfig with_variants;
    with_variants.font_path = jetbrains_font("Regular").string();
    INFO("font resolver initializes with all variants");
    REQUIRE(resolver.initialize(with_variants, 11, 96.0f));

    FontSelector selector;
    const std::string text = "A";

    struct Request
    {
        bool is_bold;
        bool is_italic;
        FontStyle expected_style;
    };
    const Request requests[] = {
        { false, false, FontStyle::Regular },
        { true, false, FontStyle::Bold },
        { false, true, FontStyle::Italic },
        { true, true, FontStyle::BoldItalic },
    };

    for (const auto& request : requests)
    {
        INFO("warm selection for " << font_style_display_name(request.expected_style));
        REQUIRE(selector.select(text, resolver, request.is_bold, request.is_italic).face
            == resolver.style(request.expected_style).font.face());
    }

    // Reinitialize with a regular-only font; the selector cache must be
    // reset (TextService does this) so every style degrades to the new
    // primary face instead of dangling on the old variant faces.
    TextServiceConfig regular_only;
    regular_only.font_path = regular_only_font().string();
    INFO("font resolver reinitializes with a regular-only font");
    REQUIRE(resolver.initialize(regular_only, 11, 96.0f));
    selector.reset_cache();

    for (const auto& request : requests)
    {
        INFO("post-reinitialize selection for " << font_style_display_name(request.expected_style));
        REQUIRE(selector.select(text, resolver, request.is_bold, request.is_italic).face
            == resolver.primary().face());
    }

    resolver.shutdown();
}

TEST_CASE("selector caches each style independently and reset clears every slot", "[font]")
{
    FontResolver resolver;
    TextServiceConfig config;
    config.font_path = jetbrains_font("Regular").string();
    INFO("font resolver initializes with all variants");
    REQUIRE(resolver.initialize(config, 11, 96.0f));

    FontSelector selector;
    const std::string text = "A";

    struct Request
    {
        bool is_bold;
        bool is_italic;
        FontStyle style;
    };
    const Request requests[] = {
        { false, false, FontStyle::Regular },
        { true, false, FontStyle::Bold },
        { false, true, FontStyle::Italic },
        { true, true, FontStyle::BoldItalic },
    };

    // A styled selection must populate only its own cache slot; the four
    // per-style caches are independent (bold caching never disturbs italic).
    for (const auto& request : requests)
    {
        selector.reset_cache();
        selector.select(text, resolver, request.is_bold, request.is_italic);

        for (size_t i = 0; i < FONT_STYLE_COUNT; ++i)
        {
            const auto style = static_cast<FontStyle>(i);
            const size_t expected = (style == request.style) ? 1u : 0u;
            INFO("request " << font_style_display_name(request.style) << " touches only the "
                            << font_style_display_name(style) << " cache");
            REQUIRE(selector.cache_size(style) == expected);
        }
    }

    // Warm every slot, then confirm reset_cache() invalidates all four at once.
    for (const auto& request : requests)
        selector.select(text, resolver, request.is_bold, request.is_italic);
    for (size_t i = 0; i < FONT_STYLE_COUNT; ++i)
    {
        INFO(font_style_display_name(static_cast<FontStyle>(i)));
        REQUIRE(selector.cache_size(static_cast<FontStyle>(i)) == 1);
    }

    selector.reset_cache();
    for (size_t i = 0; i < FONT_STYLE_COUNT; ++i)
    {
        INFO("reset clears " << font_style_display_name(static_cast<FontStyle>(i)));
        REQUIRE(selector.cache_size(static_cast<FontStyle>(i)) == 0);
    }

    resolver.shutdown();
}
