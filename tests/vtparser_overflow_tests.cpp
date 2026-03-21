#include <catch2/catch_test_macros.hpp>

#include <draxul/log.h>
#include <draxul/vt_parser.h>

#include <string>
#include <string_view>
#include <vector>

#include "test_support.h"

using namespace draxul;
using namespace draxul::tests;

namespace
{

// Build a VtParser with no-op callbacks and record CSI/OSC calls.
struct TestParser
{
    std::vector<std::string> csi_calls;
    std::vector<std::string> osc_calls;
    std::vector<std::string> clusters;
    VtParser parser;

    TestParser()
        : parser([&]() {
            VtParser::Callbacks cbs;
            cbs.on_cluster = [this](const std::string& s) { clusters.push_back(s); };
            cbs.on_control = [](char) {};
            cbs.on_csi = [this](char final_ch, std::string_view body) {
                csi_calls.push_back(std::string(body) + final_ch);
            };
            cbs.on_osc = [this](std::string_view body) { osc_calls.push_back(std::string(body)); };
            cbs.on_esc = [](char) {};
            return cbs;
        }())
    {
    }
};

} // namespace

// ---------------------------------------------------------------------------
// CSI buffer overflow tests
// ---------------------------------------------------------------------------

TEST_CASE("vtparser overflow: CSI buffer capped and no OOM", "[vtparser][overflow]")
{
    ScopedLogCapture log;
    TestParser tp;

    // Stream a CSI sequence with far more parameter bytes than kMaxCsiBuffer.
    // To avoid allocating a giant string, feed in fixed-size chunks.
    const size_t oversized = VtParser::kMaxCsiBuffer * 3;
    tp.parser.feed("\x1B["); // enter CSI state
    constexpr size_t kChunkSize = 256;
    std::string chunk(kChunkSize, '1'); // digit, not a CSI final byte
    size_t fed = 0;
    while (fed < oversized)
    {
        const size_t remaining = oversized - fed;
        const size_t n = remaining < kChunkSize ? remaining : kChunkSize;
        tp.parser.feed(std::string_view(chunk.data(), n));
        fed += n;
    }

    // At least one WARN should have been emitted for the CSI overflow.
    bool found_warn = false;
    for (const auto& rec : log.records)
    {
        if (rec.level == LogLevel::Warn && rec.message.find("CSI") != std::string::npos)
        {
            found_warn = true;
            break;
        }
    }
    REQUIRE(found_warn);

    // Parser must be usable after the overflow — feed a well-formed sequence.
    tp.parser.reset();
    tp.parser.feed("\x1B[2Ja"); // clear screen + plain 'a'
    REQUIRE_FALSE(tp.csi_calls.empty());
    REQUIRE_FALSE(tp.clusters.empty());
}

TEST_CASE("vtparser overflow: CSI buffer stays within cap", "[vtparser][overflow]")
{
    ScopedLogCapture log;
    TestParser tp;

    // Feed kMaxCsiBuffer + 100 parameter bytes (all digits = not final byte).
    const size_t oversized = VtParser::kMaxCsiBuffer + 100;
    tp.parser.feed("\x1B[");
    constexpr size_t kChunk = 128;
    std::string chunk(kChunk, '5');
    size_t fed = 0;
    while (fed < oversized)
    {
        const size_t n = (oversized - fed) < kChunk ? (oversized - fed) : kChunk;
        tp.parser.feed(std::string_view(chunk.data(), n));
        fed += n;
    }

    // At least one WARN logged.
    bool found_warn = false;
    for (const auto& rec : log.records)
    {
        if (rec.level == LogLevel::Warn && rec.message.find("CSI") != std::string::npos)
        {
            found_warn = true;
            break;
        }
    }
    REQUIRE(found_warn);

    // After overflow, parser should return to Ground — feeding a normal 'm'
    // (a CSI final byte) should NOT trigger another csi call for the old data.
    const size_t csi_before = tp.csi_calls.size();
    tp.parser.feed("m"); // 'm' is a CSI final byte but parser is in Ground now
    // No new CSI call triggered from Ground state for plain 'm'.
    // (It would appear as a cluster, not a CSI sequence.)
    REQUIRE(tp.csi_calls.size() == csi_before);
}

// ---------------------------------------------------------------------------
// OSC buffer overflow tests
// ---------------------------------------------------------------------------

TEST_CASE("vtparser overflow: OSC buffer capped and no OOM", "[vtparser][overflow]")
{
    ScopedLogCapture log;
    TestParser tp;

    // Feed an OSC sequence that exceeds kMaxOscBuffer with no terminator.
    const size_t oversized = VtParser::kMaxOscBuffer * 3;
    tp.parser.feed("\x1B]"); // enter OSC state
    constexpr size_t kChunk = 256;
    std::string chunk(kChunk, 'x');
    size_t fed = 0;
    while (fed < oversized)
    {
        const size_t n = (oversized - fed) < kChunk ? (oversized - fed) : kChunk;
        tp.parser.feed(std::string_view(chunk.data(), n));
        fed += n;
    }

    bool found_warn = false;
    for (const auto& rec : log.records)
    {
        if (rec.level == LogLevel::Warn && rec.message.find("OSC") != std::string::npos)
        {
            found_warn = true;
            break;
        }
    }
    REQUIRE(found_warn);

    // Parser still usable.
    tp.parser.reset();
    tp.parser.feed("\x1B]0;title\x07"); // normal OSC with BEL terminator
    REQUIRE_FALSE(tp.osc_calls.empty());
    REQUIRE(tp.osc_calls.back() == "0;title");
}

TEST_CASE("vtparser overflow: OSC buffer stays within cap", "[vtparser][overflow]")
{
    ScopedLogCapture log;
    TestParser tp;

    const size_t oversized = VtParser::kMaxOscBuffer + 50;
    tp.parser.feed("\x1B]");
    constexpr size_t kChunk = 128;
    std::string chunk(kChunk, 'y');
    size_t fed = 0;
    while (fed < oversized)
    {
        const size_t n = (oversized - fed) < kChunk ? (oversized - fed) : kChunk;
        tp.parser.feed(std::string_view(chunk.data(), n));
        fed += n;
    }

    bool found_warn = false;
    for (const auto& rec : log.records)
    {
        if (rec.level == LogLevel::Warn && rec.message.find("OSC") != std::string::npos)
        {
            found_warn = true;
            break;
        }
    }
    REQUIRE(found_warn);

    // A BEL now should not generate an OSC call because state is Ground.
    const size_t osc_before = tp.osc_calls.size();
    tp.parser.feed("\x07"); // BEL in Ground = control character, not OSC terminator
    REQUIRE(tp.osc_calls.size() == osc_before);
}

// ---------------------------------------------------------------------------
// plain_text buffer overflow test
// ---------------------------------------------------------------------------

TEST_CASE("vtparser overflow: plain_text buffer capped for incomplete multibyte", "[vtparser][overflow]")
{
    // plain_text_ only grows when incomplete UTF-8 multi-byte sequences stall
    // the flush loop.  We simulate this by feeding a sequence of 0xC2 bytes
    // (2-byte UTF-8 lead), each of which is incomplete without a continuation
    // byte, causing the buffer to accumulate without being flushed.

    ScopedLogCapture log;
    TestParser tp;

    const size_t oversized = VtParser::kMaxPlainTextBuffer + 100;
    // 0xC2 = two-byte sequence lead (needs one continuation byte 0x80–0xBF).
    // Feeding it alone leaves an incomplete codepoint that stalls flush.
    // However, each new 0xC2 will also be preceded by a flush attempt.
    // We rely on the cap check before push_back to bound growth.
    constexpr size_t kChunk = 512;
    std::string chunk(kChunk, '\xC2');
    size_t fed = 0;
    while (fed < oversized)
    {
        const size_t n = (oversized - fed) < kChunk ? (oversized - fed) : kChunk;
        tp.parser.feed(std::string_view(chunk.data(), n));
        fed += n;
    }

    bool found_warn = false;
    for (const auto& rec : log.records)
    {
        if (rec.level == LogLevel::Warn && rec.message.find("plain_text") != std::string::npos)
        {
            found_warn = true;
            break;
        }
    }
    REQUIRE(found_warn);

    // Parser still usable after overflow — feed valid ASCII.
    tp.parser.reset();
    tp.parser.feed("hello");
    REQUIRE_FALSE(tp.clusters.empty());
}

// ---------------------------------------------------------------------------
// Regression: normal sequences still work after overflow guard is in place
// ---------------------------------------------------------------------------

TEST_CASE("vtparser overflow: normal sequences unaffected by cap guards", "[vtparser][overflow]")
{
    ScopedLogCapture log;
    TestParser tp;

    // Small CSI within cap.
    tp.parser.feed("\x1B[1;32m");
    REQUIRE_FALSE(tp.csi_calls.empty());

    // Small OSC within cap.
    tp.parser.feed("\x1B]0;My Title\x07");
    REQUIRE_FALSE(tp.osc_calls.empty());
    REQUIRE(tp.osc_calls.back() == "0;My Title");

    // Plain text.
    tp.parser.feed("hello");
    REQUIRE_FALSE(tp.clusters.empty());

    // No spurious warnings for normal input.
    bool has_warn = false;
    for (const auto& rec : log.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            has_warn = true;
            break;
        }
    }
    REQUIRE_FALSE(has_warn);
}
