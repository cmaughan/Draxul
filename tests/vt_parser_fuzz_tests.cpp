#include "support/test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <draxul/vt_parser.h>

#include <cstdlib>
#include <string>
#include <string_view>

using namespace draxul;

namespace
{

// Build a VtParser with no-op callbacks for all events.
VtParser make_noop_parser()
{
    VtParser::Callbacks cbs;
    cbs.on_cluster = [](const std::string&) {};
    cbs.on_control = [](char) {};
    cbs.on_csi = [](char, std::string_view) {};
    cbs.on_osc = [](std::string_view) {};
    cbs.on_esc = [](char) {};
    return VtParser(std::move(cbs));
}

// Feed bytes into the parser and assert no crash or exception.
// Also verifies the parser is still usable after the feed.
void assert_safe_feed(VtParser& parser, std::string_view bytes)
{
    // Must not throw or crash.
    parser.feed(bytes);
    // Parser should still be usable after feeding malformed input.
    parser.feed("");
}

} // namespace

TEST_CASE("vt parser fuzz: empty buffer", "[vt_parser][fuzz]")
{
    auto parser = make_noop_parser();
    assert_safe_feed(parser, "");
}

TEST_CASE("vt parser fuzz: bare ESC byte", "[vt_parser][fuzz]")
{
    auto parser = make_noop_parser();
    assert_safe_feed(parser, "\x1B");
}

TEST_CASE("vt parser fuzz: truncated CSI sequence", "[vt_parser][fuzz]")
{
    auto parser = make_noop_parser();
    // CSI introducer with no final byte
    assert_safe_feed(parser, "\x1B[");
    parser.reset();
    // CSI with param but no final byte
    assert_safe_feed(parser, "\x1B[1");
    parser.reset();
    // CSI with partial params
    assert_safe_feed(parser, "\x1B[1;");
}

TEST_CASE("vt parser fuzz: truncated OSC sequence", "[vt_parser][fuzz]")
{
    auto parser = make_noop_parser();
    // OSC introducer with no content or terminator
    assert_safe_feed(parser, "\x1B]");
    parser.reset();
    // 4000-byte OSC body with no terminator (test buffer growth)
    std::string long_osc = "\x1B]";
    long_osc += std::string(4000, 'x');
    assert_safe_feed(parser, long_osc);
    parser.reset();
    // OSC with title but no BEL or ST terminator
    assert_safe_feed(parser, "\x1B]0;Window Title");
}

TEST_CASE("vt parser fuzz: null bytes in various positions", "[vt_parser][fuzz]")
{
    auto parser = make_noop_parser();
    // Single null
    assert_safe_feed(parser, std::string_view("\x00", 1));
    parser.reset();
    // Null inside CSI
    assert_safe_feed(parser, std::string_view("\x1B[\x00", 3));
    parser.reset();
    // Null inside OSC
    assert_safe_feed(parser, std::string_view("\x1B]\x00", 3));
    parser.reset();
    // 100 null bytes
    assert_safe_feed(parser, std::string(100, '\x00'));
}

TEST_CASE("vt parser fuzz: all 256 single-byte values", "[vt_parser][fuzz]")
{
    for (int b = 0; b < 256; ++b)
    {
        auto parser = make_noop_parser();
        char ch = static_cast<char>(b);
        assert_safe_feed(parser, std::string_view(&ch, 1));
    }
}

TEST_CASE("vt parser fuzz: corpus of known-tricky sequences", "[vt_parser][fuzz]")
{
    const std::string_view sequences[] = {
        "\x1B[?2004h", // bracketed paste mode
        "\x1B[?1049h", // alt screen
        "\x1B[2J", // clear screen
        "\x1B[1;32m", // SGR color
        "\x1B"
        "c", // full reset ESC c (split to avoid \x1Bc hex ambiguity)
        "\x1B"
        "7\x1B"
        "8", // DECSC/DECRC
        "\x1B[H\x1B[2J", // move to origin + clear
        "\x0D\x0A", // CR LF
        "\x08", // backspace
        "\x07", // BEL
        "\x1B]0;title\x07", // OSC title with BEL terminator
        "\x1B]0;title\x1B\\", // OSC title with ST terminator
    };

    for (std::string_view seq : sequences)
    {
        auto parser = make_noop_parser();
        // Assert no exception thrown.
        assert_safe_feed(parser, seq);
        // Parser still usable after.
        assert_safe_feed(parser, "a");
    }
}

TEST_CASE("vt parser fuzz: 10000 random-pattern inputs", "[vt_parser][fuzz][slow]")
{
    DRAXUL_SKIP_UNLESS_SLOW();

    // Simple LCG PRNG seeded with 42 for deterministic output.
    uint64_t state = 42;
    auto lcg_next = [&]() -> uint64_t {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state;
    };

    for (int i = 0; i < 10000; ++i)
    {
        // Length 1–20
        int len = static_cast<int>((lcg_next() % 20) + 1);
        std::string input;
        input.reserve(static_cast<size_t>(len));
        for (int j = 0; j < len; ++j)
        {
            input.push_back(static_cast<char>(lcg_next() & 0xFF));
        }

        auto parser = make_noop_parser();
        // Must not crash for any random input.
        assert_safe_feed(parser, input);
    }
}
