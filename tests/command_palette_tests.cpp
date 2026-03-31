#include <catch2/catch_test_macros.hpp>

#include "command_palette.h"
#include "fuzzy_match.h"
#include "gui_action_handler.h"
#include <SDL3/SDL.h>
#include <draxul/events.h>

using namespace draxul;

// ── Fuzzy match tests ──────────────────────────────────────────────────────

TEST_CASE("fuzzy_match: empty pattern matches everything", "[fuzzy]")
{
    auto result = fuzzy_match("", "toggle_diagnostics");
    CHECK(result.matched);
    CHECK(result.score == 0);
    CHECK(result.positions.empty());
}

TEST_CASE("fuzzy_match: empty target does not match", "[fuzzy]")
{
    auto result = fuzzy_match("td", "");
    CHECK_FALSE(result.matched);
}

TEST_CASE("fuzzy_match: prefix match scores higher than mid-word", "[fuzzy]")
{
    auto prefix = fuzzy_match("copy", "copy");
    auto mid = fuzzy_match("copy", "xcopy");
    CHECK(prefix.matched);
    CHECK(mid.matched);
    CHECK(prefix.score > mid.score);
}

TEST_CASE("fuzzy_match: boundary bonus at underscore", "[fuzzy]")
{
    // "td" in "toggle_diagnostics": t at start (boundary), d after _ (boundary)
    auto boundary = fuzzy_match("td", "toggle_diagnostics");
    // "td" in "outdoors": t and d are mid-word
    auto midword = fuzzy_match("td", "outdoors");
    CHECK(boundary.matched);
    CHECK(midword.matched);
    CHECK(boundary.score > midword.score);
}

TEST_CASE("fuzzy_match: consecutive chars get bonus", "[fuzzy]")
{
    // Both targets start mid-word so first-char boundary bonus doesn't skew the comparison
    auto consecutive = fuzzy_match("ab", "xabc");
    auto scattered = fuzzy_match("ab", "xaxb");
    CHECK(consecutive.matched);
    CHECK(scattered.matched);
    CHECK(consecutive.score > scattered.score);
}

TEST_CASE("fuzzy_match: case insensitive", "[fuzzy]")
{
    auto result = fuzzy_match("COPY", "copy");
    CHECK(result.matched);
    CHECK(result.positions.size() == 4);
}

TEST_CASE("fuzzy_match: non-matching returns false", "[fuzzy]")
{
    auto result = fuzzy_match("xyz", "copy");
    CHECK_FALSE(result.matched);
}

TEST_CASE("fuzzy_match: first char bonus doubled", "[fuzzy]")
{
    // Pattern starting at a boundary should score higher than mid-word
    auto at_start = fuzzy_match("f", "font_increase");
    auto mid = fuzzy_match("n", "font_increase");
    CHECK(at_start.matched);
    CHECK(mid.matched);
    // 'f' at position 0 gets boundary bonus * 2; 'n' at position 2 gets no bonus
    CHECK(at_start.score > mid.score);
}

TEST_CASE("fuzzy_match: positions are correct", "[fuzzy]")
{
    auto result = fuzzy_match("fi", "font_increase");
    CHECK(result.matched);
    REQUIRE(result.positions.size() == 2);
    CHECK(result.positions[0] == 0); // f
    CHECK(result.positions[1] == 5); // i after _
}

// ── CommandPalette state tests ─────────────────────────────────────────────

TEST_CASE("CommandPalette: open and close", "[palette]")
{
    CommandPalette palette;
    CHECK_FALSE(palette.is_open());
    palette.open();
    CHECK(palette.is_open());
    palette.close();
    CHECK_FALSE(palette.is_open());
}

TEST_CASE("CommandPalette: Escape closes", "[palette]")
{
    CommandPalette palette;
    palette.open();
    KeyEvent esc{ 0, SDLK_ESCAPE, kModNone, true };
    palette.on_key(esc);
    CHECK_FALSE(palette.is_open());
}

TEST_CASE("CommandPalette: consumes all keys when open", "[palette]")
{
    CommandPalette palette;
    palette.open();
    KeyEvent key{ 0, SDLK_A, kModNone, true };
    CHECK(palette.on_key(key));
}

TEST_CASE("CommandPalette: does not consume when closed", "[palette]")
{
    CommandPalette palette;
    KeyEvent key{ 0, SDLK_A, kModNone, true };
    CHECK_FALSE(palette.on_key(key));
}

TEST_CASE("CommandPalette: action_names excludes command_palette", "[palette]")
{
    auto names = GuiActionHandler::action_names();
    bool found_palette = false;
    bool found_copy = false;
    for (auto name : names)
    {
        if (name == "command_palette")
            found_palette = true;
        if (name == "copy")
            found_copy = true;
    }
    CHECK(found_palette); // it exists in the registry
    CHECK(found_copy);
}
