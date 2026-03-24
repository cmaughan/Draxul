#include "support/test_support.h"

#include <draxul/grid.h>
#include <draxul/log.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <string>
#include <string_view>

using namespace draxul;
using namespace draxul::tests;

namespace
{

// Helper: verify that a CellText view is valid UTF-8 (every codepoint is complete).
static bool is_valid_utf8(std::string_view s)
{
    size_t i = 0;
    while (i < s.size())
    {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        size_t seq_len = 0;
        if (b < 0x80)
            seq_len = 1;
        else if ((b & 0xE0) == 0xC0)
            seq_len = 2;
        else if ((b & 0xF0) == 0xE0)
            seq_len = 3;
        else if ((b & 0xF8) == 0xF0)
            seq_len = 4;
        else
            return false;

        if (i + seq_len > s.size())
            return false;
        for (size_t j = 1; j < seq_len; ++j)
        {
            if ((static_cast<uint8_t>(s[i + j]) & 0xC0) != 0x80)
                return false;
        }
        i += seq_len;
    }
    return true;
}

// A canary-guarded pair of CellTexts: [0] = target, [1] = canary.
// We fill canary with a known pattern and verify it is unmodified after
// assigning to the target.
struct CanaryPair
{
    CellText cells[2];
    static constexpr uint8_t kCanaryByte = 0xCD;

    CanaryPair()
    {
        // Fill canary cell with a known byte pattern.
        std::memset(&cells[1], kCanaryByte, sizeof(CellText));
    }

    CellText& target()
    {
        return cells[0];
    }

    bool canary_intact() const
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&cells[1]);
        for (size_t i = 0; i < sizeof(CellText); ++i)
        {
            if (bytes[i] != kCanaryByte)
                return false;
        }
        return true;
    }
};

struct CorpusEntry
{
    const char* label;
    std::string_view sequence;
    // Expected: len <= kMaxLen, and the stored text is a valid UTF-8 prefix.
};

// -- Corpus sequences --

// 1. Family emoji with skin tones: 👨‍👩‍👧‍👦
//    U+1F468 U+200D U+1F469 U+200D U+1F467 U+200D U+1F466
//    Each emoji is 4 bytes, each ZWJ is 3 bytes => 4+3+4+3+4+3+4 = 25 bytes.
//    This fits in 32 bytes.
static const std::string kFamilyEmoji = "\xF0\x9F\x91\xA8" // U+1F468
                                        "\xE2\x80\x8D" // U+200D ZWJ
                                        "\xF0\x9F\x91\xA9" // U+1F469
                                        "\xE2\x80\x8D" // U+200D ZWJ
                                        "\xF0\x9F\x91\xA7" // U+1F467
                                        "\xE2\x80\x8D" // U+200D ZWJ
                                        "\xF0\x9F\x91\xA6"; // U+1F466

// 2. Eye-in-speech-bubble ZWJ sequence: 👁️‍🗨️
//    U+1F441 U+FE0F U+200D U+1F5E8 U+FE0F
//    4 + 3 + 3 + 4 + 3 = 17 bytes.
static const std::string kEyeSpeechBubble = "\xF0\x9F\x91\x81" // U+1F441
                                            "\xEF\xB8\x8F" // U+FE0F VS16
                                            "\xE2\x80\x8D" // U+200D ZWJ
                                            "\xF0\x9F\x97\xA8" // U+1F5E8
                                            "\xEF\xB8\x8F"; // U+FE0F VS16

// 3. Long combining sequence: base 'a' + 8 combining diacritical marks (U+0300..U+0307).
//    Each combining mark is 2 bytes UTF-8 (0xCC 0x80..0x87).
//    Total: 1 + 8*2 = 17 bytes.
static const std::string kLongCombining = "a"
                                          "\xCC\x80" // U+0300 COMBINING GRAVE
                                          "\xCC\x81" // U+0301 COMBINING ACUTE
                                          "\xCC\x82" // U+0302 COMBINING CIRCUMFLEX
                                          "\xCC\x83" // U+0303 COMBINING TILDE
                                          "\xCC\x84" // U+0304 COMBINING MACRON
                                          "\xCC\x85" // U+0305 COMBINING OVERLINE
                                          "\xCC\x86" // U+0306 COMBINING BREVE
                                          "\xCC\x87"; // U+0307 COMBINING DOT ABOVE

// 4. RTL mark sequences: Arabic text with explicit RTL marks.
//    U+200F (RLM, 3 bytes) + U+0627 (ALEF, 2 bytes) + U+0644 (LAM, 2 bytes)
//    + U+0639 (AIN, 2 bytes) + U+200F (RLM, 3 bytes)
//    Total: 3+2+2+2+3 = 12 bytes.
static const std::string kRtlMarks = "\xE2\x80\x8F" // U+200F RLM
                                     "\xD8\xA7" // U+0627 ALEF
                                     "\xD9\x84" // U+0644 LAM
                                     "\xD8\xB9" // U+0639 AIN
                                     "\xE2\x80\x8F"; // U+200F RLM

// 5. Exactly-at-boundary: a 32-byte sequence (8 four-byte emoji codepoints).
//    8 * U+1F600 (GRINNING FACE) = 8 * 4 = 32 bytes exactly.
static const std::string kExact32 = "\xF0\x9F\x98\x80" // U+1F600
                                    "\xF0\x9F\x98\x80"
                                    "\xF0\x9F\x98\x80"
                                    "\xF0\x9F\x98\x80"
                                    "\xF0\x9F\x98\x80"
                                    "\xF0\x9F\x98\x80"
                                    "\xF0\x9F\x98\x80"
                                    "\xF0\x9F\x98\x80";

// 6. Over-boundary: 33 bytes (8 four-byte emoji + 1 ASCII byte). Must truncate.
static const std::string kOver32 = kExact32 + "X";

// 7. Family emoji with skin tone modifiers (exceeds 32 bytes).
//    U+1F468 U+1F3FB U+200D U+1F469 U+1F3FC U+200D U+1F467 U+1F3FD U+200D U+1F466 U+1F3FE
//    Each emoji: 4 bytes, each skin tone: 4 bytes, each ZWJ: 3 bytes.
//    4+4+3 + 4+4+3 + 4+4+3 + 4+4 = 41 bytes. Exceeds kMaxLen.
static const std::string kFamilySkinTone = "\xF0\x9F\x91\xA8" // U+1F468
                                           "\xF0\x9F\x8F\xBB" // U+1F3FB light skin
                                           "\xE2\x80\x8D" // ZWJ
                                           "\xF0\x9F\x91\xA9" // U+1F469
                                           "\xF0\x9F\x8F\xBC" // U+1F3FC medium-light
                                           "\xE2\x80\x8D" // ZWJ
                                           "\xF0\x9F\x91\xA7" // U+1F467
                                           "\xF0\x9F\x8F\xBD" // U+1F3FD medium
                                           "\xE2\x80\x8D" // ZWJ
                                           "\xF0\x9F\x91\xA6" // U+1F466
                                           "\xF0\x9F\x8F\xBE"; // U+1F3FE medium-dark

} // namespace

TEST_CASE("CellText corpus: family emoji ZWJ (fits in 32 bytes)", "[celltext][unicode]")
{
    REQUIRE(kFamilyEmoji.size() == 25);

    CanaryPair pair;
    pair.target().assign(kFamilyEmoji);

    CHECK(pair.target().len == 25);
    CHECK(pair.target().view() == kFamilyEmoji);
    CHECK(is_valid_utf8(pair.target().view()));
    CHECK(pair.canary_intact());
}

TEST_CASE("CellText corpus: eye-in-speech-bubble ZWJ", "[celltext][unicode]")
{
    REQUIRE(kEyeSpeechBubble.size() == 17);

    CanaryPair pair;
    pair.target().assign(kEyeSpeechBubble);

    CHECK(pair.target().len == 17);
    CHECK(pair.target().view() == kEyeSpeechBubble);
    CHECK(is_valid_utf8(pair.target().view()));
    CHECK(pair.canary_intact());
}

TEST_CASE("CellText corpus: long combining sequence", "[celltext][unicode]")
{
    REQUIRE(kLongCombining.size() == 17);

    CanaryPair pair;
    pair.target().assign(kLongCombining);

    CHECK(pair.target().len == 17);
    CHECK(pair.target().view() == kLongCombining);
    CHECK(is_valid_utf8(pair.target().view()));
    CHECK(pair.canary_intact());
}

TEST_CASE("CellText corpus: RTL mark sequences", "[celltext][unicode]")
{
    REQUIRE(kRtlMarks.size() == 12);

    CanaryPair pair;
    pair.target().assign(kRtlMarks);

    CHECK(pair.target().len == 12);
    CHECK(pair.target().view() == kRtlMarks);
    CHECK(is_valid_utf8(pair.target().view()));
    CHECK(pair.canary_intact());
}

TEST_CASE("CellText corpus: exactly 32 bytes (boundary)", "[celltext][unicode]")
{
    REQUIRE(kExact32.size() == 32);

    CanaryPair pair;
    pair.target().assign(kExact32);

    CHECK(pair.target().len == 32);
    CHECK(pair.target().view() == kExact32);
    CHECK(is_valid_utf8(pair.target().view()));
    CHECK(pair.canary_intact());
}

TEST_CASE("CellText corpus: 33 bytes truncates to valid UTF-8 prefix", "[celltext][unicode]")
{
    ScopedLogCapture log(LogLevel::Warn);

    REQUIRE(kOver32.size() == 33);

    CanaryPair pair;
    pair.target().assign(kOver32);

    // Should truncate to 32 bytes (the last ASCII 'X' is dropped, but the 8 emoji fit exactly).
    CHECK(pair.target().len == 32);
    CHECK(pair.target().view() == kExact32);
    CHECK(is_valid_utf8(pair.target().view()));
    CHECK(pair.canary_intact());

    // Should have emitted a truncation warning.
    REQUIRE(log.records.size() >= 1);
    CHECK(log.records[0].level == LogLevel::Warn);
}

TEST_CASE("CellText corpus: family emoji with skin tones (exceeds 32 bytes)", "[celltext][unicode]")
{
    ScopedLogCapture log(LogLevel::Warn);

    REQUIRE(kFamilySkinTone.size() == 41);

    CanaryPair pair;
    pair.target().assign(kFamilySkinTone);

    // Must truncate to a valid UTF-8 prefix <= 32 bytes.
    CHECK(pair.target().len <= CellText::kMaxLen);
    CHECK(pair.target().len > 0);
    CHECK(is_valid_utf8(pair.target().view()));
    CHECK(pair.canary_intact());

    // The stored text must be a prefix of the original.
    CHECK(kFamilySkinTone.substr(0, pair.target().len) == pair.target().view());

    // Should have emitted a truncation warning.
    REQUIRE(log.records.size() >= 1);
    CHECK(log.records[0].level == LogLevel::Warn);
}

TEST_CASE("CellText corpus: assign empty string", "[celltext][unicode]")
{
    CanaryPair pair;
    pair.target().assign("");

    CHECK(pair.target().len == 0);
    CHECK(pair.target().empty());
    CHECK(pair.canary_intact());
}

TEST_CASE("CellText corpus: reassign overwrites previous content", "[celltext][unicode]")
{
    CanaryPair pair;
    pair.target().assign(kFamilyEmoji);
    CHECK(pair.target().len == 25);

    pair.target().assign("A");
    CHECK(pair.target().len == 1);
    CHECK(pair.target().view() == "A");
    CHECK(pair.canary_intact());
}
