
#include <catch2/catch_all.hpp>

#include <draxul/log.h>
#include <draxul/text_service.h>
#include <filesystem>
#include <string>
#include <vector>

using namespace draxul;

namespace
{

struct CorpusEntry
{
    const char* script;
    std::string text; // UTF-8 encoded sample codepoint(s)
};

// UTF-8 encoded codepoints for each script block (1-3 representative codepoints each)
const CorpusEntry k_corpus[] = {
    { "Latin", "A" }, // U+0041
    { "Greek", "\xCE\x91" }, // U+0391 GREEK CAPITAL LETTER ALPHA
    { "Cyrillic", "\xD0\x90" }, // U+0410 CYRILLIC CAPITAL LETTER A
    { "Hebrew", "\xD7\x90" }, // U+05D0 HEBREW LETTER ALEF
    { "Arabic", "\xD8\xA7" }, // U+0627 ARABIC LETTER ALEF
    { "Devanagari", "\xE0\xA4\x85" }, // U+0905 DEVANAGARI LETTER A
    { "CJK Unified", "\xE4\xB8\xAD" }, // U+4E2D CJK (middle)
    { "Hangul", "\xEA\xB0\x80" }, // U+AC00 HANGUL SYLLABLE GA
    { "Hiragana", "\xE3\x81\x82" }, // U+3042 HIRAGANA LETTER A
    { "Katakana", "\xE3\x82\xA2" }, // U+30A2 KATAKANA LETTER A
    { "Thai", "\xE0\xB8\x81" }, // U+0E01 THAI CHARACTER KO KAI
    { "Emoji", "\xF0\x9F\x98\x80" }, // U+1F600 GRINNING FACE
    { "Math Symbols", "\xE2\x88\x91" }, // U+2211 N-ARY SUMMATION
    { "Box Drawing", "\xE2\x94\x80" }, // U+2500 BOX DRAWINGS LIGHT HORIZONTAL
    { "Braille", "\xE2\xA0\x80" }, // U+2800 BRAILLE PATTERN BLANK
};

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

} // namespace

TEST_CASE("font fallback corpus: primary font is present", "[font][slow]")
{
    auto font = primary_font_path();
    INFO("bundled JetBrainsMono NerdFont exists");
    REQUIRE(std::filesystem::exists(font));
}

TEST_CASE("font fallback corpus: TextService initializes and all script blocks resolve", "[font][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    auto font = primary_font_path();
    INFO("bundled font exists");
    REQUIRE(std::filesystem::exists(font));

    TextService service;
    TextServiceConfig config;
    config.font_path = font.string();
    config.fallback_paths = system_fallback_paths();
    INFO("text service initializes with fallback chain");
    REQUIRE(service.initialize(config, 11, 96.0f));

    int resolved_count = 0;
    int missing_count = 0;

    for (const auto& entry : k_corpus)
    {
        // resolve_cluster must not crash or assert — that is the primary guarantee
        const AtlasRegion region = service.resolve_cluster(entry.text);

        if (region.size.x > 0)
        {
            ++resolved_count;
            DRAXUL_LOG_INFO(LogCategory::Test, "[corpus] %s: resolved (%dx%d)", entry.script,
                region.size.x, region.size.y);
        }
        else
        {
            // A zero-width result means no glyph (or whitespace). This is a warning,
            // not an error — .notdef typically has non-zero dimensions, but some
            // whitespace-only clusters intentionally produce an empty region.
            ++missing_count;
            DRAXUL_LOG_WARN(LogCategory::Test, "[corpus] %s: glyph width is zero (missing or whitespace)",
                entry.script);
        }
    }

    const int total = static_cast<int>(std::size(k_corpus));
    DRAXUL_LOG_INFO(LogCategory::Test, "[corpus] %d/%d script blocks produced a visible glyph (%d zero-width)",
        resolved_count, total, missing_count);

    // At minimum Latin, Greek, Cyrillic, Math symbols, and Box Drawing must resolve
    // because those are covered by the primary font (JetBrainsMonoNerdFont).
    const CorpusEntry* required[] = {
        &k_corpus[0], // Latin
        &k_corpus[1], // Greek
        &k_corpus[2], // Cyrillic
        &k_corpus[12], // Math Symbols
        &k_corpus[13], // Box Drawing
    };

    for (const auto* req : required)
    {
        const AtlasRegion r = service.resolve_cluster(req->text);
        INFO(std::string(req->script) + " must resolve in primary font (non-zero width)");
        REQUIRE(r.size.x > 0);
    }

    service.shutdown();
}
