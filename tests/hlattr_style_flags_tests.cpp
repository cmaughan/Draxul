
#include <catch2/catch_all.hpp>
#include <draxul/highlight.h>
#include <draxul/types.h>

using namespace draxul;

// Work item 27 (hlattr-style-flags-fix) replaced the magic literals in
// HlAttr::style_flags() with named STYLE_FLAG_* constants from types.h.
// These tests verify that style_flags() returns the correct bit positions
// and that the named constants match the implementation.
//
// style_flags() mapping (from highlight.h, using STYLE_FLAG_* constants):
//   bold         -> STYLE_FLAG_BOLD          (1u << 0 = 1)
//   italic       -> STYLE_FLAG_ITALIC        (1u << 1 = 2)
//   underline    -> STYLE_FLAG_UNDERLINE     (1u << 2 = 4)
//   strikethrough-> STYLE_FLAG_STRIKETHROUGH (1u << 3 = 8)
//   undercurl    -> STYLE_FLAG_UNDERCURL     (1u << 4 = 16)

TEST_CASE("hlattr style_flags no flags set returns zero", "[grid]")
{
    HlAttr attr;
    INFO("default HlAttr with no style flags should return 0");
    REQUIRE(attr.style_flags() == static_cast<uint32_t>(0));
}

TEST_CASE("hlattr style_flags bold only sets bold bit", "[grid]")
{
    HlAttr attr;
    attr.bold = true;
    const uint32_t flags = attr.style_flags();
    INFO("bold flag should be set");
    REQUIRE((flags & STYLE_FLAG_BOLD) != 0);
    INFO("italic flag should be clear");
    REQUIRE((flags & STYLE_FLAG_ITALIC) == 0);
    INFO("underline flag should be clear");
    REQUIRE((flags & STYLE_FLAG_UNDERLINE) == 0);
    INFO("strikethrough flag should be clear");
    REQUIRE((flags & STYLE_FLAG_STRIKETHROUGH) == 0);
    INFO("undercurl flag should be clear");
    REQUIRE((flags & STYLE_FLAG_UNDERCURL) == 0);
}

TEST_CASE("hlattr style_flags italic only sets italic bit", "[grid]")
{
    HlAttr attr;
    attr.italic = true;
    const uint32_t flags = attr.style_flags();
    INFO("italic flag should be set");
    REQUIRE((flags & STYLE_FLAG_ITALIC) != 0);
    INFO("bold flag should be clear");
    REQUIRE((flags & STYLE_FLAG_BOLD) == 0);
    INFO("underline flag should be clear");
    REQUIRE((flags & STYLE_FLAG_UNDERLINE) == 0);
    INFO("strikethrough flag should be clear");
    REQUIRE((flags & STYLE_FLAG_STRIKETHROUGH) == 0);
    INFO("undercurl flag should be clear");
    REQUIRE((flags & STYLE_FLAG_UNDERCURL) == 0);
}

TEST_CASE("hlattr style_flags underline only sets underline bit", "[grid]")
{
    HlAttr attr;
    attr.underline = true;
    const uint32_t flags = attr.style_flags();
    INFO("underline flag should be set");
    REQUIRE((flags & STYLE_FLAG_UNDERLINE) != 0);
    INFO("bold flag should be clear");
    REQUIRE((flags & STYLE_FLAG_BOLD) == 0);
    INFO("italic flag should be clear");
    REQUIRE((flags & STYLE_FLAG_ITALIC) == 0);
    INFO("strikethrough flag should be clear");
    REQUIRE((flags & STYLE_FLAG_STRIKETHROUGH) == 0);
    INFO("undercurl flag should be clear");
    REQUIRE((flags & STYLE_FLAG_UNDERCURL) == 0);
}

TEST_CASE("hlattr style_flags strikethrough only sets strikethrough bit", "[grid]")
{
    HlAttr attr;
    attr.strikethrough = true;
    const uint32_t flags = attr.style_flags();
    INFO("strikethrough flag should be set");
    REQUIRE((flags & STYLE_FLAG_STRIKETHROUGH) != 0);
    INFO("bold flag should be clear");
    REQUIRE((flags & STYLE_FLAG_BOLD) == 0);
    INFO("italic flag should be clear");
    REQUIRE((flags & STYLE_FLAG_ITALIC) == 0);
    INFO("underline flag should be clear");
    REQUIRE((flags & STYLE_FLAG_UNDERLINE) == 0);
    INFO("undercurl flag should be clear");
    REQUIRE((flags & STYLE_FLAG_UNDERCURL) == 0);
}

TEST_CASE("hlattr style_flags undercurl only sets undercurl bit", "[grid]")
{
    HlAttr attr;
    attr.undercurl = true;
    const uint32_t flags = attr.style_flags();
    INFO("undercurl flag should be set");
    REQUIRE((flags & STYLE_FLAG_UNDERCURL) != 0);
    INFO("bold flag should be clear");
    REQUIRE((flags & STYLE_FLAG_BOLD) == 0);
    INFO("italic flag should be clear");
    REQUIRE((flags & STYLE_FLAG_ITALIC) == 0);
    INFO("underline flag should be clear");
    REQUIRE((flags & STYLE_FLAG_UNDERLINE) == 0);
    INFO("strikethrough flag should be clear");
    REQUIRE((flags & STYLE_FLAG_STRIKETHROUGH) == 0);
}

TEST_CASE("hlattr style_flags all flags set returns all bits", "[grid]")
{
    HlAttr attr;
    attr.bold = true;
    attr.italic = true;
    attr.underline = true;
    attr.strikethrough = true;
    attr.undercurl = true;

    const uint32_t flags = attr.style_flags();
    INFO("bold flag should be set");
    REQUIRE((flags & STYLE_FLAG_BOLD) != 0);
    INFO("italic flag should be set");
    REQUIRE((flags & STYLE_FLAG_ITALIC) != 0);
    INFO("underline flag should be set");
    REQUIRE((flags & STYLE_FLAG_UNDERLINE) != 0);
    INFO("strikethrough flag should be set");
    REQUIRE((flags & STYLE_FLAG_STRIKETHROUGH) != 0);
    INFO("undercurl flag should be set");
    REQUIRE((flags & STYLE_FLAG_UNDERCURL) != 0);

    const uint32_t expected_all = STYLE_FLAG_BOLD | STYLE_FLAG_ITALIC | STYLE_FLAG_UNDERLINE
        | STYLE_FLAG_STRIKETHROUGH | STYLE_FLAG_UNDERCURL;
    INFO("all five style flags should combine to the expected bitmask");
    REQUIRE(flags == expected_all);
}

TEST_CASE("hlattr style flag constants are distinct powers of two", "[grid]")
{
    // Each constant must be a non-zero power of two (exactly one bit set).
    auto is_power_of_two = [](uint32_t v) -> bool {
        return v != 0 && (v & (v - 1)) == 0;
    };

    INFO("STYLE_FLAG_BOLD is a power of two");
    REQUIRE(is_power_of_two(STYLE_FLAG_BOLD));
    INFO("STYLE_FLAG_ITALIC is a power of two");
    REQUIRE(is_power_of_two(STYLE_FLAG_ITALIC));
    INFO("STYLE_FLAG_UNDERLINE is a power of two");
    REQUIRE(is_power_of_two(STYLE_FLAG_UNDERLINE));
    INFO("STYLE_FLAG_STRIKETHROUGH is a power of two");
    REQUIRE(is_power_of_two(STYLE_FLAG_STRIKETHROUGH));
    INFO("STYLE_FLAG_UNDERCURL is a power of two");
    REQUIRE(is_power_of_two(STYLE_FLAG_UNDERCURL));

    // No two constants share a bit.
    INFO("BOLD and ITALIC are distinct");
    REQUIRE(STYLE_FLAG_BOLD != STYLE_FLAG_ITALIC);
    INFO("BOLD and UNDERLINE are distinct");
    REQUIRE(STYLE_FLAG_BOLD != STYLE_FLAG_UNDERLINE);
    INFO("BOLD and STRIKETHROUGH are distinct");
    REQUIRE(STYLE_FLAG_BOLD != STYLE_FLAG_STRIKETHROUGH);
    INFO("BOLD and UNDERCURL are distinct");
    REQUIRE(STYLE_FLAG_BOLD != STYLE_FLAG_UNDERCURL);
    INFO("ITALIC and UNDERLINE are distinct");
    REQUIRE(STYLE_FLAG_ITALIC != STYLE_FLAG_UNDERLINE);
    INFO("ITALIC and STRIKETHROUGH are distinct");
    REQUIRE(STYLE_FLAG_ITALIC != STYLE_FLAG_STRIKETHROUGH);
    INFO("ITALIC and UNDERCURL are distinct");
    REQUIRE(STYLE_FLAG_ITALIC != STYLE_FLAG_UNDERCURL);
    INFO("UNDERLINE and STRIKETHROUGH are distinct");
    REQUIRE(STYLE_FLAG_UNDERLINE != STYLE_FLAG_STRIKETHROUGH);
    INFO("UNDERLINE and UNDERCURL are distinct");
    REQUIRE(STYLE_FLAG_UNDERLINE != STYLE_FLAG_UNDERCURL);
    INFO("STRIKETHROUGH and UNDERCURL are distinct");
    REQUIRE(STYLE_FLAG_STRIKETHROUGH != STYLE_FLAG_UNDERCURL);
}

TEST_CASE("hlattr style_flags bold constant matches implementation bit value", "[grid]")
{
    HlAttr attr;
    attr.bold = true;
    INFO("bold alone should equal STYLE_FLAG_BOLD (1)");
    REQUIRE(attr.style_flags() == STYLE_FLAG_BOLD);
}

TEST_CASE("hlattr style_flags italic constant matches implementation bit value", "[grid]")
{
    HlAttr attr;
    attr.italic = true;
    INFO("italic alone should equal STYLE_FLAG_ITALIC (2)");
    REQUIRE(attr.style_flags() == STYLE_FLAG_ITALIC);
}

TEST_CASE("hlattr style_flags underline constant matches implementation bit value", "[grid]")
{
    HlAttr attr;
    attr.underline = true;
    INFO("underline alone should equal STYLE_FLAG_UNDERLINE (4)");
    REQUIRE(attr.style_flags() == STYLE_FLAG_UNDERLINE);
}

TEST_CASE("hlattr style_flags strikethrough constant matches implementation bit value", "[grid]")
{
    HlAttr attr;
    attr.strikethrough = true;
    INFO("strikethrough alone should equal STYLE_FLAG_STRIKETHROUGH (8)");
    REQUIRE(attr.style_flags() == STYLE_FLAG_STRIKETHROUGH);
}

TEST_CASE("hlattr style_flags undercurl constant matches implementation bit value", "[grid]")
{
    HlAttr attr;
    attr.undercurl = true;
    INFO("undercurl alone should equal STYLE_FLAG_UNDERCURL (16)");
    REQUIRE(attr.style_flags() == STYLE_FLAG_UNDERCURL);
}
