#include <catch2/catch_all.hpp>

#include "../libs/draxul-font/src/font_style.h"

#include <set>
#include <string>

using namespace draxul;

// Table tests for the indexed FontStyle model that replaced the repeated
// normal/bold/italic/bold-italic branches in the font resolver and selector
// (kanban: 30 font-style-model -refactor).

namespace
{

struct StyleTraitRow
{
    FontStyle style;
    bool is_bold;
    bool is_italic;
    const char* display_name;
    const char* lower_name;
    const char* file_suffix;
};

constexpr StyleTraitRow STYLE_TRAIT_ROWS[] = {
    { FontStyle::Regular, false, false, "Regular", "regular", "Regular" },
    { FontStyle::Bold, true, false, "Bold", "bold", "Bold" },
    { FontStyle::Italic, false, true, "Italic", "italic", "Italic" },
    { FontStyle::BoldItalic, true, true, "Bold-italic", "bold-italic", "BoldItalic" },
};

} // namespace

TEST_CASE("font style traits are consistent for every style", "[font]")
{
    STATIC_REQUIRE(FONT_STYLE_COUNT == 4);

    std::set<size_t> seen_indices;
    for (const auto& row : STYLE_TRAIT_ROWS)
    {
        INFO(row.display_name);

        INFO("flags -> style mapping is exact");
        REQUIRE(font_style_from_flags(row.is_bold, row.is_italic) == row.style);

        INFO("style -> flags round-trips");
        REQUIRE(font_style_is_bold(row.style) == row.is_bold);
        REQUIRE(font_style_is_italic(row.style) == row.is_italic);

        INFO("diagnostic names match the resolver's historical messages");
        REQUIRE(std::string(font_style_display_name(row.style)) == row.display_name);
        REQUIRE(std::string(font_style_lower_name(row.style)) == row.lower_name);

        INFO("auto-detect filename token matches the -Regular replacement");
        REQUIRE(std::string(font_style_file_suffix(row.style)) == row.file_suffix);

        INFO("indices are in range and unique");
        REQUIRE(font_style_index(row.style) < FONT_STYLE_COUNT);
        seen_indices.insert(font_style_index(row.style));
    }

    INFO("all four styles occupy distinct array slots");
    REQUIRE(seen_indices.size() == FONT_STYLE_COUNT);
}

TEST_CASE("font style flag round-trip is total over all flag combinations", "[font]")
{
    for (bool is_bold : { false, true })
    {
        for (bool is_italic : { false, true })
        {
            const FontStyle style = font_style_from_flags(is_bold, is_italic);
            INFO("bold=" << is_bold << " italic=" << is_italic);
            REQUIRE(font_style_is_bold(style) == is_bold);
            REQUIRE(font_style_is_italic(style) == is_italic);
        }
    }
}

TEST_CASE("font style variant list covers exactly the non-regular styles", "[font]")
{
    STATIC_REQUIRE(FONT_STYLE_VARIANTS.size() == FONT_STYLE_COUNT - 1);

    std::set<FontStyle> variants(FONT_STYLE_VARIANTS.begin(), FONT_STYLE_VARIANTS.end());
    INFO("variants are unique");
    REQUIRE(variants.size() == FONT_STYLE_VARIANTS.size());
    INFO("regular is not a variant");
    REQUIRE(variants.count(FontStyle::Regular) == 0);
}

TEST_CASE("font style fallback chains prefer exact then degraded variants", "[font]")
{
    SECTION("bold-italic degrades through bold then italic")
    {
        const auto chain = font_style_fallback_chain(FontStyle::BoldItalic);
        REQUIRE(chain.size() == 3);
        REQUIRE(chain.styles[0] == FontStyle::BoldItalic);
        REQUIRE(chain.styles[1] == FontStyle::Bold);
        REQUIRE(chain.styles[2] == FontStyle::Italic);
    }

    SECTION("bold tries only the bold variant")
    {
        const auto chain = font_style_fallback_chain(FontStyle::Bold);
        REQUIRE(chain.size() == 1);
        REQUIRE(chain.styles[0] == FontStyle::Bold);
    }

    SECTION("italic tries only the italic variant")
    {
        const auto chain = font_style_fallback_chain(FontStyle::Italic);
        REQUIRE(chain.size() == 1);
        REQUIRE(chain.styles[0] == FontStyle::Italic);
    }

    SECTION("regular has no variant chain")
    {
        const auto chain = font_style_fallback_chain(FontStyle::Regular);
        REQUIRE(chain.size() == 0);
        REQUIRE(chain.begin() == chain.end());
    }

    SECTION("no chain routes a requested style through a contradictory variant")
    {
        // Bold must never degrade to Italic and vice versa — only BoldItalic
        // may degrade across the bold/italic axis.
        for (FontStyle requested : { FontStyle::Bold, FontStyle::Italic })
        {
            for (FontStyle candidate : font_style_fallback_chain(requested))
            {
                INFO("requested " << font_style_display_name(requested));
                REQUIRE(candidate == requested);
            }
        }
    }
}
