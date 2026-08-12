#include <catch2/catch_test_macros.hpp>

#include <draxul/vt_parser.h>

#include <string>
#include <string_view>
#include <vector>

using namespace draxul;

namespace
{

std::string hex_byte(unsigned char byte)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    return std::string{ "0x" } + kHex[byte >> 4] + kHex[byte & 0x0F];
}

struct TraceHarness
{
    std::vector<std::string> events;
    VtParser parser;

    TraceHarness()
        : parser([this]() {
            VtParser::Callbacks cbs;
            cbs.on_cluster = [this](const std::string& cluster) {
                events.push_back("cluster:" + cluster);
            };
            cbs.on_control = [this](char ch) {
                events.push_back("control:" + hex_byte(static_cast<unsigned char>(ch)));
            };
            cbs.on_csi = [this](char final_ch, std::string_view body) {
                events.push_back("csi:" + std::string(body) + "|" + std::string(1, final_ch));
            };
            cbs.on_osc = [this](std::string_view body) {
                events.push_back("osc:" + std::string(body));
            };
            cbs.on_esc = [this](char ch) {
                events.push_back("esc:" + std::string(1, ch));
            };
            cbs.on_dcs = [this](std::string_view body) {
                events.push_back("dcs:" + std::string(body));
            };
            return cbs;
        }())
    {
    }
};

std::vector<std::string> capture_events(std::string_view bytes)
{
    TraceHarness harness;
    harness.parser.feed(bytes);
    return harness.events;
}

std::vector<std::string> capture_events_split(std::string_view bytes, size_t split)
{
    TraceHarness harness;
    harness.parser.feed(bytes.substr(0, split));
    harness.parser.feed(bytes.substr(split));
    return harness.events;
}

struct SequenceCase
{
    const char* name;
    std::string bytes;
};

} // namespace

TEST_CASE("vt parser partial sequences: split feeds match whole feed", "[vt_parser][partial]")
{
    const std::string plain_utf8 = std::string("plain ") + "\xE2\x98\x83" + " text";
    const std::vector<SequenceCase> cases = {
        { "CSI clear screen", "\x1B[2J" },
        { "SGR with multiple params", "\x1B[1;31m" },
        { "DEC private mode", "\x1B[?1049h" },
        { "OSC title", "\x1B]0;title\x07" },
        { "DCS XTGETTCAP", "\x1BP+q544e\x1B\\" },
        { "plain ASCII text", "plain text" },
        { "plain UTF-8 text", plain_utf8 },
    };

    for (const auto& seq : cases)
    {
        const auto baseline = capture_events(seq.bytes);

        for (size_t split = 1; split < seq.bytes.size(); ++split)
        {
            CAPTURE(seq.name, split);
            const auto split_events = capture_events_split(seq.bytes, split);
            REQUIRE(split_events == baseline);
        }
    }
}
