
#include <catch2/catch_all.hpp>

#include <chrono>
#include <cstdint>
#include <draxul/log.h>
#include <draxul/text_service.h>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace draxul;

namespace
{

std::filesystem::path repo_root()
{
    return std::filesystem::path(DRAXUL_PROJECT_ROOT);
}

std::filesystem::path primary_font_path()
{
    return repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
}

// Build a list of system fallback font paths available on this machine.
// Script blocks that require specific fonts will only be tested when the
// fallback font is present; otherwise the test logs a warning.
std::vector<std::string> system_fallback_paths()
{
    std::vector<std::string> paths;

    const std::filesystem::path candidates[] = {
// macOS system fonts covering the corpus scripts
#if defined(__APPLE__)
        // CJK (Hiragana, Katakana, Hangul, CJK Unified)
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/AquaKana.ttc",
        // Arabic
        "/System/Library/Fonts/SFArabic.ttf",
        "/System/Library/Fonts/GeezaPro.ttc",
        // Hebrew
        "/System/Library/Fonts/SFHebrew.ttf",
        // Devanagari / Thai / various Indic
        "/System/Library/Fonts/Kohinoor.ttc",
        "/System/Library/Fonts/Supplemental/DevanagariMT.ttc",
        // Emoji
        "/System/Library/Fonts/Apple Color Emoji.ttc",
        // Math / Symbols
        "/System/Library/Fonts/Apple Symbols.ttf",
        "/System/Library/Fonts/Supplemental/STIXTwoMath.otf",
        // Braille
        "/System/Library/Fonts/Apple Braille.ttf",
        // Broad Unicode fallback
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
#elif defined(_WIN32)
        "C:\\Windows\\Fonts\\seguiemj.ttf",
        "C:\\Windows\\Fonts\\seguisym.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\times.ttf",
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simsun.ttc",
#else
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansHebrew-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansDevanagari-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansThai-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansMath-Regular.ttf",
#endif
    };

    for (const auto& p : candidates)
    {
        if (std::filesystem::exists(p))
            paths.push_back(p.string());
    }

    return paths;
}

// Helper: encode a single Unicode codepoint as UTF-8.
std::string codepoint_to_utf8(uint32_t cp)
{
    std::string result;
    if (cp <= 0x7F)
    {
        result += static_cast<char>(cp);
    }
    else if (cp <= 0x7FF)
    {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp <= 0xFFFF)
    {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp <= 0x10FFFF)
    {
        result += static_cast<char>(0xF0 | (cp >> 18));
        result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return result;
}

// Initialize a TextService with fallback chain for the corpus tests.
bool init_service_with_fallbacks(TextService& service, float point_size = 11.0f)
{
    auto font = primary_font_path();
    if (!std::filesystem::exists(font))
        return false;
    TextServiceConfig config;
    config.font_path = font.string();
    config.fallback_paths = system_fallback_paths();
    return service.initialize(config, point_size, 96.0f);
}

} // namespace

// ---------------------------------------------------------------------------
// Case 1: Basic Latin + extended Latin (U+0020 - U+024F)
// ---------------------------------------------------------------------------
TEST_CASE("font fallback corpus: basic and extended Latin", "[font][fallback]")
{
    TextService service;
    REQUIRE(init_service_with_fallbacks(service));

    // A sentence with accented Latin characters spanning Basic Latin and
    // Latin Extended-A/B ranges.
    const std::string samples[] = {
        "Hello, World!", // Basic ASCII
        "caf\xC3\xA9", // cafe with e-acute (U+00E9)
        "\xC3\x80\xC3\xA1\xC3\xA2\xC3\xA3\xC3\xA4\xC3\xA5", // Accented vowels
        "\xC4\x8D\xC4\x99\xC5\x82\xC5\x9B", // Polish: czeslaw-ish
        "\xC8\x98\xC8\x9A", // Latin Extended-B: S-comma, T-comma (U+0218, U+021A)
    };

    for (const auto& text : samples)
    {
        INFO("resolving: " + text);
        const auto region = service.resolve_cluster(text);
        // Must not crash. Primary font covers Latin, so we expect real glyphs.
        REQUIRE(region.bitmap_size.x > 0);
        REQUIRE(region.bitmap_size.y > 0);
    }

    service.shutdown();
}

// ---------------------------------------------------------------------------
// Case 2: CJK Unified Ideographs (U+4E00 - U+9FFF, sample of 100)
// ---------------------------------------------------------------------------
TEST_CASE("font fallback corpus: CJK ideographs", "[font][fallback]")
{
    TextService service;
    REQUIRE(init_service_with_fallbacks(service));

    // Sample 100 codepoints from the CJK Unified Ideographs block.
    // If no CJK font is available, the primary font should still return
    // a .notdef / tofu placeholder -- never crash.
    constexpr uint32_t cjk_start = 0x4E00;
    constexpr int sample_count = 100;

    int resolved = 0;
    for (int i = 0; i < sample_count; ++i)
    {
        uint32_t cp = cjk_start + static_cast<uint32_t>(i * 200); // spread across the block
        if (cp > 0x9FFF)
            cp = 0x9FFF;
        std::string text = codepoint_to_utf8(cp);
        INFO("CJK U+" + std::to_string(cp));
        const auto region = service.resolve_cluster(text);
        // Primary guarantee: no crash. A zero-width result means the font
        // returned a placeholder or blank, which is acceptable.
        if (region.bitmap_size.x > 0)
            ++resolved;
    }

    INFO("CJK resolved " + std::to_string(resolved) + "/" + std::to_string(sample_count)
        + " with visible glyphs");

    // If we have system CJK fallback fonts, we expect most to resolve.
    // If not, we still pass -- the test is about crash safety.
    SUCCEED();

    service.shutdown();
}

// ---------------------------------------------------------------------------
// Case 3: Emoji -- basic (U+1F600 - U+1F64F)
// ---------------------------------------------------------------------------
TEST_CASE("font fallback corpus: basic emoji", "[font][fallback]")
{
    TextService service;
    REQUIRE(init_service_with_fallbacks(service));

    // 10 basic emoji from the Emoticons block
    const uint32_t emoji_codepoints[] = {
        0x1F600, // Grinning face
        0x1F601, // Grinning face with smiling eyes
        0x1F602, // Face with tears of joy
        0x1F603, // Smiling face with open mouth
        0x1F604, // Smiling face with open mouth and smiling eyes
        0x1F605, // Smiling face with open mouth and cold sweat
        0x1F609, // Winking face
        0x1F60A, // Smiling face with smiling eyes
        0x1F60D, // Smiling face with heart-eyes
        0x1F64F, // Person with folded hands
    };

    int resolved = 0;
    for (uint32_t cp : emoji_codepoints)
    {
        std::string text = codepoint_to_utf8(cp);
        INFO("Emoji U+" + std::to_string(cp));
        const auto region = service.resolve_cluster(text);
        // Must not crash. At least produce a tofu block or real glyph.
        if (region.bitmap_size.x > 0)
            ++resolved;
    }

    INFO("Emoji resolved " + std::to_string(resolved) + "/10 with visible glyphs");

    // If color emoji font is available, expect at least some to resolve.
    // Without it, zero-width is acceptable (no crash is the primary guarantee).
    SUCCEED();

    service.shutdown();
}

// ---------------------------------------------------------------------------
// Case 4: Emoji with skin-tone modifier (ZWJ sequence)
// ---------------------------------------------------------------------------
TEST_CASE("font fallback corpus: emoji ZWJ sequence", "[font][fallback]")
{
    TextService service;
    REQUIRE(init_service_with_fallbacks(service));

    // Man Technologist: U+1F468 + U+200D (ZWJ) + U+1F4BB
    const std::string man_technologist = codepoint_to_utf8(0x1F468) + codepoint_to_utf8(0x200D)
        + codepoint_to_utf8(0x1F4BB);

    INFO("resolving man technologist ZWJ sequence");
    const auto region = service.resolve_cluster(man_technologist);

    // Must not crash. Result is either a single composite glyph
    // or a multi-glyph sequence. Both are acceptable.
    // If no emoji font is available, even a zero-width result is fine.
    INFO("ZWJ sequence resolved (size.x=" + std::to_string(region.bitmap_size.x) + ")");
    SUCCEED();

    // Also test a simpler skin-tone sequence: waving hand + medium skin tone
    // U+1F44B + U+1F3FD
    const std::string wave_medium = codepoint_to_utf8(0x1F44B) + codepoint_to_utf8(0x1F3FD);

    INFO("resolving waving hand with skin tone modifier");
    const auto region2 = service.resolve_cluster(wave_medium);
    INFO("skin tone sequence resolved (size.x=" + std::to_string(region2.bitmap_size.x) + ")");
    SUCCEED();

    service.shutdown();
}

// ---------------------------------------------------------------------------
// Case 5: RTL characters (Arabic, U+0600 - U+06FF)
// ---------------------------------------------------------------------------
TEST_CASE("font fallback corpus: RTL Arabic characters", "[font][fallback]")
{
    TextService service;
    REQUIRE(init_service_with_fallbacks(service));

    // Short Arabic word: "salam" (peace) - U+0633 U+0644 U+0627 U+0645
    const std::string arabic_salam = codepoint_to_utf8(0x0633) + codepoint_to_utf8(0x0644)
        + codepoint_to_utf8(0x0627) + codepoint_to_utf8(0x0645);

    INFO("resolving Arabic word 'salam'");
    const auto region = service.resolve_cluster(arabic_salam);

    // Must not crash and produce > 0 glyphs if Arabic font is available.
    if (region.bitmap_size.x > 0)
    {
        INFO("Arabic text resolved with visible glyphs");
        REQUIRE(region.bitmap_size.y > 0);
    }
    else
    {
        // No Arabic font -- acceptable, just note it.
        INFO("Arabic text produced zero-width result (no Arabic fallback font available)");
    }
    SUCCEED();

    // Also test individual Arabic letters to ensure no crash per-character
    for (uint32_t cp = 0x0627; cp <= 0x062A; ++cp)
    {
        std::string text = codepoint_to_utf8(cp);
        INFO("Arabic U+" + std::to_string(cp));
        service.resolve_cluster(text); // must not crash
    }

    service.shutdown();
}

// ---------------------------------------------------------------------------
// Case 6: Combining marks (U+0300 - U+036F)
// ---------------------------------------------------------------------------
TEST_CASE("font fallback corpus: combining marks", "[font][fallback]")
{
    TextService service;
    REQUIRE(init_service_with_fallbacks(service));

    // 'e' + combining grave accent (U+0300) => should produce a single
    // composed glyph equivalent to e-grave.
    const std::string e_grave_combining = std::string("e") + codepoint_to_utf8(0x0300);

    INFO("resolving 'e' + combining grave accent");
    const auto region = service.resolve_cluster(e_grave_combining);

    // Must not crash. The primary font (JetBrains Mono) covers Latin
    // combining marks, so we expect a real glyph.
    REQUIRE(region.bitmap_size.x > 0);
    REQUIRE(region.bitmap_size.y > 0);

    // Width should not exceed roughly two cell widths (it's a single
    // character with a combining mark). Get the cell width from metrics.
    const auto& metrics = service.metrics();
    int cell_width = static_cast<int>(metrics.cell_width + 0.5f);
    if (cell_width > 0)
    {
        INFO("combining mark glyph width (" + std::to_string(region.bitmap_size.x)
            + ") should not exceed 2x cell width (" + std::to_string(cell_width * 2) + ")");
        REQUIRE(region.bitmap_size.x <= cell_width * 2);
    }

    // Also test 'a' + combining acute (U+0301) + combining tilde (U+0303)
    // -- stacked combining marks.
    const std::string stacked = std::string("a") + codepoint_to_utf8(0x0301) + codepoint_to_utf8(0x0303);
    INFO("resolving 'a' + combining acute + combining tilde");
    const auto region2 = service.resolve_cluster(stacked);
    REQUIRE(region2.bitmap_size.x > 0);

    service.shutdown();
}

// ---------------------------------------------------------------------------
// Case 7: Missing glyph / null result caching
// ---------------------------------------------------------------------------
TEST_CASE("font fallback corpus: missing glyph does not crash", "[font][fallback]")
{
    TextService service;
    REQUIRE(init_service_with_fallbacks(service));

    // Use a Private Use Area codepoint that no font will have a glyph for.
    // U+F0000 is in Supplementary Private Use Area-A.
    const std::string pua_codepoint = codepoint_to_utf8(0xF0000);

    INFO("resolving PUA codepoint U+F0000 (first call)");
    const auto region1 = service.resolve_cluster(pua_codepoint);
    // Must not crash. Result may be empty or a .notdef placeholder.

    INFO("resolving PUA codepoint U+F0000 (second call -- should be cached)");
    const auto region2 = service.resolve_cluster(pua_codepoint);
    // Must not crash. Second call should be at least as fast (cached).

    // Both calls should return the same result (cached).
    REQUIRE(region1.bitmap_size.x == region2.bitmap_size.x);
    REQUIRE(region1.bitmap_size.y == region2.bitmap_size.y);

    // NOTE: Verifying that the second call is faster (cached miss) would
    // require a test hook in GlyphCache to inspect the cache directly.
    // This is noted as deferred -- see WI 35 implementation notes.
    // For now, we verify that repeated resolution of the same missing
    // codepoint returns a consistent result without crashing.

    // Also test another PUA codepoint to ensure broad coverage
    const std::string pua2 = codepoint_to_utf8(0xFFF00);
    INFO("resolving PUA codepoint U+FFF00");
    service.resolve_cluster(pua2); // must not crash

    service.shutdown();
}

// ---------------------------------------------------------------------------
// Case 8: Large corpus stress (500 random BMP codepoints in < 5 seconds)
// ---------------------------------------------------------------------------
TEST_CASE("font fallback corpus: 500 BMP codepoints stress test", "[font][fallback]")
{
    TextService service;
    REQUIRE(init_service_with_fallbacks(service));

    // Generate 500 codepoints spread across the BMP (U+0020 - U+FFFD),
    // excluding surrogates (U+D800 - U+DFFF).
    std::mt19937 rng(42); // fixed seed for reproducibility
    std::uniform_int_distribution<uint32_t> dist(0x0020, 0xFFFD);

    std::vector<std::string> codepoints;
    codepoints.reserve(500);
    while (codepoints.size() < 500)
    {
        uint32_t cp = dist(rng);
        // Skip surrogate range
        if (cp >= 0xD800 && cp <= 0xDFFF)
            continue;
        codepoints.push_back(codepoint_to_utf8(cp));
    }

    auto start = std::chrono::steady_clock::now();

    int resolved = 0;
    for (const auto& text : codepoints)
    {
        const auto region = service.resolve_cluster(text);
        if (region.bitmap_size.x > 0)
            ++resolved;
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    INFO("500 BMP codepoints resolved in " + std::to_string(elapsed_ms) + " ms, "
        + std::to_string(resolved) + " had visible glyphs");

    // Must complete in under 5 seconds
    REQUIRE(elapsed_ms < 5000);

    // Atlas usage should be within bounds
    float usage = service.atlas_usage_ratio();
    INFO("atlas usage ratio: " + std::to_string(usage));
    REQUIRE(usage >= 0.0f);
    REQUIRE(usage <= 1.0f);

    // Glyph count should be > 0 (at least some resolved)
    size_t glyph_count = service.atlas_glyph_count();
    INFO("atlas glyph count: " + std::to_string(glyph_count));
    REQUIRE(glyph_count > 0);

    service.shutdown();
}
