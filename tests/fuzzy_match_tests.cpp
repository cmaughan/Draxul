#include <draxul/fuzzy_match.h>

#include <catch2/catch_test_macros.hpp>

using draxul::fuzzy_match;

TEST_CASE("fuzzy_match: empty pattern matches anything with score 0", "[fuzzy_match]")
{
    const auto r = fuzzy_match("", "split_vertical");
    REQUIRE(r.matched);
    REQUIRE(r.score == 0);
    REQUIRE(r.positions.empty());
}

TEST_CASE("fuzzy_match: empty target with non-empty pattern fails", "[fuzzy_match]")
{
    const auto r = fuzzy_match("split", "");
    REQUIRE_FALSE(r.matched);
}

TEST_CASE("fuzzy_match: pattern longer than target fails", "[fuzzy_match]")
{
    const auto r = fuzzy_match("split_horizontal", "split");
    REQUIRE_FALSE(r.matched);
}

TEST_CASE("fuzzy_match: non-matching pattern returns no match", "[fuzzy_match]")
{
    const auto r = fuzzy_match("xyz", "split_vertical");
    REQUIRE_FALSE(r.matched);
}

TEST_CASE("fuzzy_match: exact target and prefix-in-longer-target score equally", "[fuzzy_match]")
{
    // The scoring function intentionally does not penalise unmatched trailing
    // characters in the target — both runs match the same indices with the same
    // bonuses. The command palette breaks the tie by preferring the shorter
    // target (see command_palette.cpp refilter() comparator).
    const auto exact = fuzzy_match("split", "split");
    const auto prefix = fuzzy_match("split", "split_vertical");
    REQUIRE(exact.matched);
    REQUIRE(prefix.matched);
    REQUIRE(exact.score == prefix.score);
}

TEST_CASE("fuzzy_match: contiguous prefix scores higher than scattered subsequence", "[fuzzy_match]")
{
    const auto prefix = fuzzy_match("split", "split_vertical");
    const auto subseq = fuzzy_match("sv", "split_vertical");
    REQUIRE(prefix.matched);
    REQUIRE(subseq.matched);
    // The contiguous "split" run earns consecutive bonuses and a bigger first-char
    // bonus, so it must outscore the scattered subsequence.
    REQUIRE(prefix.score > subseq.score);
}

TEST_CASE("fuzzy_match: contiguous match always beats gappy match in same target", "[fuzzy_match]")
{
    struct Pair
    {
        const char* contiguous;
        const char* gappy;
        const char* target;
    };
    const Pair pairs[] = {
        { "open", "opn", "open_file" },
        { "close", "cle", "close_window" },
        { "copy", "cpy", "copy_to_clipboard" },
        { "new", "nt", "new_tab" },
        { "font", "fnt", "font_increase" },
    };
    for (const auto& p : pairs)
    {
        INFO("contiguous=" << p.contiguous << " gappy=" << p.gappy << " target=" << p.target);
        const auto a = fuzzy_match(p.contiguous, p.target);
        const auto b = fuzzy_match(p.gappy, p.target);
        REQUIRE(a.matched);
        REQUIRE(b.matched);
        REQUIRE(a.score > b.score);
    }
}

TEST_CASE("fuzzy_match: subsequence positions are correct and in-bounds", "[fuzzy_match]")
{
    const std::string target = "split_vertical";
    const auto r = fuzzy_match("sv", target);
    REQUIRE(r.matched);
    REQUIRE(r.positions.size() == 2);
    REQUIRE(r.positions[0] == 0); // 's' in "split"
    REQUIRE(r.positions[1] == 6); // 'v' in "vertical"
    for (size_t pos : r.positions)
        REQUIRE(pos < target.size());
}

TEST_CASE("fuzzy_match: positions are strictly increasing (non-overlapping)", "[fuzzy_match]")
{
    const auto r = fuzzy_match("slit", "split_vertical");
    REQUIRE(r.matched);
    REQUIRE(r.positions.size() == 4);
    for (size_t i = 1; i < r.positions.size(); ++i)
        REQUIRE(r.positions[i] > r.positions[i - 1]);
}

TEST_CASE("fuzzy_match: case-insensitive matching", "[fuzzy_match]")
{
    const auto a = fuzzy_match("SPLIT", "split_vertical");
    const auto b = fuzzy_match("split", "SPLIT_VERTICAL");
    REQUIRE(a.matched);
    REQUIRE(b.matched);
    REQUIRE(a.positions == std::vector<size_t>{ 0, 1, 2, 3, 4 });
    REQUIRE(b.positions == std::vector<size_t>{ 0, 1, 2, 3, 4 });
}

TEST_CASE("fuzzy_match: single-char first position beats later position", "[fuzzy_match]")
{
    const auto first = fuzzy_match("c", "copy");
    const auto later = fuzzy_match("c", "abc");
    REQUIRE(first.matched);
    REQUIRE(later.matched);
    REQUIRE(first.score > later.score);
}

TEST_CASE("fuzzy_match: word-boundary char beats mid-word char", "[fuzzy_match]")
{
    // After '_' delimiter, 'v' picks up the boundary bonus.
    const auto boundary = fuzzy_match("v", "split_vertical");
    const auto midword = fuzzy_match("v", "having");
    REQUIRE(boundary.matched);
    REQUIRE(midword.matched);
    REQUIRE(boundary.score > midword.score);
}

TEST_CASE("fuzzy_match: positions index the correct characters in target", "[fuzzy_match]")
{
    const std::string target = "Split_Vertical";
    const auto r = fuzzy_match("sv", target);
    REQUIRE(r.matched);
    REQUIRE(r.positions.size() == 2);
    // Verify the indexed chars actually match the pattern (case-insensitive).
    REQUIRE(std::tolower(static_cast<unsigned char>(target[r.positions[0]])) == 's');
    REQUIRE(std::tolower(static_cast<unsigned char>(target[r.positions[1]])) == 'v');
}
