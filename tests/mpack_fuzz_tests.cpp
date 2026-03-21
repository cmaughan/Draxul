
#include <draxul/mpack_codec.h>

#include <catch2/catch_all.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace draxul;

namespace
{

// Decode bytes and verify the function either succeeds with a well-defined
// MpackValue or returns false with the out-value left in a safe state.
// Must not throw, crash, or produce UB.
void assert_safe_decode(std::span<const uint8_t> bytes, std::string_view label)
{
    MpackValue value;
    size_t consumed = 0;
    bool ok = decode_mpack_value(bytes, value, &consumed);
    if (ok)
    {
        // On success the consumed count must be within the buffer.
        INFO(std::string(label) + ": consumed <= input size");
        REQUIRE(consumed <= bytes.size());
        // The value must have a valid type index (not an out-of-range variant).
        MpackValue::Type t = value.type();
        INFO(std::string(label) + ": returned type is a valid enum value");
        REQUIRE(t >= MpackValue::Nil);
        REQUIRE(t <= MpackValue::Ext);
    }
    // On failure the return value is false; no further requirements on 'value'.
}

} // namespace

TEST_CASE("mpack fuzz: empty buffer (0 bytes)", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    assert_safe_decode({}, "empty buffer");
}

TEST_CASE("mpack fuzz: single-byte buffers (all 256 values)", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    for (int b = 0; b < 256; ++b)
    {
        uint8_t byte = static_cast<uint8_t>(b);
        assert_safe_decode({ &byte, 1 }, "single byte 0x" + std::to_string(b));
    }
}

TEST_CASE("mpack fuzz: fixarray header truncated body", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0x93 = fixarray of length 3, 0xc3 = true (one element only)
    const uint8_t buf[] = { 0x93, 0xc3 };
    assert_safe_decode(buf, "fixarray truncated");
}

TEST_CASE("mpack fuzz: fixmap header truncated body", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0x81 = fixmap of length 1 (needs key+value); supply only one byte
    const uint8_t buf[] = { 0x81, 0xa3 };
    assert_safe_decode(buf, "fixmap truncated");
}

TEST_CASE("mpack fuzz: str8 header truncated body", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0xd9 = str8, length=5, but no payload bytes follow
    const uint8_t buf[] = { 0xd9, 0x05 };
    assert_safe_decode(buf, "str8 truncated");
}

TEST_CASE("mpack fuzz: bin8 header truncated body", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0xc4 = bin8, length=4, but no payload bytes follow
    const uint8_t buf[] = { 0xc4, 0x04 };
    assert_safe_decode(buf, "bin8 truncated");
}

TEST_CASE("mpack fuzz: array16 header truncated body", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0xdc = array16, count=3 (2 count bytes), no elements
    const uint8_t buf[] = { 0xdc, 0x00, 0x03 };
    assert_safe_decode(buf, "array16 truncated");
}

TEST_CASE("mpack fuzz: uint64 header truncated body", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0xcf = uint64 — requires 8 more bytes; supply only 3
    const uint8_t buf[] = { 0xcf, 0x01, 0x02, 0x03 };
    assert_safe_decode(buf, "uint64 truncated");
}

TEST_CASE("mpack fuzz: deeply nested arrays (10 levels)", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    std::vector<uint8_t> buf;
    for (int i = 0; i < 10; ++i)
        buf.push_back(0x91); // fixarray(1)
    buf.push_back(0xc0); // nil at the innermost level
    assert_safe_decode(buf, "deep nesting 10 levels");
}

TEST_CASE("mpack fuzz: deeply nested arrays (50 levels)", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    std::vector<uint8_t> buf;
    for (int i = 0; i < 50; ++i)
        buf.push_back(0x91);
    buf.push_back(0xc0);
    assert_safe_decode(buf, "deep nesting 50 levels");
}

TEST_CASE("mpack fuzz: deeply nested arrays (50 levels) truncated at leaf", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    std::vector<uint8_t> buf;
    for (int i = 0; i < 50; ++i)
        buf.push_back(0x91);
    // Deliberately omit the leaf — body is missing
    assert_safe_decode(buf, "deep nesting 50 levels truncated");
}

TEST_CASE("mpack fuzz: ext type byte 0 (neovim buffer handle)", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    const uint8_t buf[] = { 0xd4, 0x00, 0x01 };
    assert_safe_decode(buf, "ext type 0");
}

TEST_CASE("mpack fuzz: ext type byte 127 (max positive)", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    const uint8_t buf[] = { 0xd4, 0x7f, 0xff };
    assert_safe_decode(buf, "ext type 127");
}

TEST_CASE("mpack fuzz: ext type byte -1 (0xff signed)", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    const uint8_t buf[] = { 0xd4, 0xff, 0x00 };
    assert_safe_decode(buf, "ext type -1");
}

TEST_CASE("mpack fuzz: ext type byte -128 (0x80 signed, min int8)", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    const uint8_t buf[] = { 0xd4, 0x80, 0x00 };
    assert_safe_decode(buf, "ext type -128");
}

TEST_CASE("mpack fuzz: fixext4 with unexpected type byte", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0xd6 = fixext4, type=0x42, 4 data bytes
    const uint8_t buf[] = { 0xd6, 0x42, 0xde, 0xad, 0xbe, 0xef };
    assert_safe_decode(buf, "fixext4 type 0x42");
}

TEST_CASE("mpack fuzz: ext8 with large payload (>8 bytes — skipped path)", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0xc7 = ext8, length=16, type=1, 16 zero bytes
    std::vector<uint8_t> buf = { 0xc7, 0x10, 0x01 };
    for (int i = 0; i < 16; ++i)
        buf.push_back(0x00);
    assert_safe_decode(buf, "ext8 payload >8 bytes");
}

TEST_CASE("mpack fuzz: ext8 header present but payload truncated", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0xc7 = ext8, length=10, type=2, but no payload
    const uint8_t buf[] = { 0xc7, 0x0a, 0x02 };
    assert_safe_decode(buf, "ext8 truncated payload");
}

TEST_CASE("mpack fuzz: fixstr with overlong UTF-8 sequence", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0xa4 = fixstr length 4, followed by invalid UTF-8 (0xfe 0xfe 0xfe 0xfe)
    const uint8_t buf[] = { 0xa4, 0xfe, 0xfe, 0xfe, 0xfe };
    assert_safe_decode(buf, "fixstr invalid utf-8 overlong");
}

TEST_CASE("mpack fuzz: fixstr with lone continuation bytes", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0xa3 = fixstr length 3; 0x80 0x81 0x82 are bare continuation bytes
    const uint8_t buf[] = { 0xa3, 0x80, 0x81, 0x82 };
    assert_safe_decode(buf, "fixstr lone continuation bytes");
}

TEST_CASE("mpack fuzz: fixstr with truncated multi-byte sequence", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0xa2 = fixstr length 2; 0xc3 starts a 2-byte seq but next byte 0x28 is not continuation
    const uint8_t buf[] = { 0xa2, 0xc3, 0x28 };
    assert_safe_decode(buf, "fixstr truncated multi-byte sequence");
}

TEST_CASE("mpack fuzz: fixstr with null bytes embedded", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    const uint8_t buf[] = { 0xa4, 0x00, 0x00, 0x00, 0x00 };
    assert_safe_decode(buf, "fixstr embedded nulls");
}

TEST_CASE("mpack fuzz: all-zeros buffer length 1", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    const uint8_t buf[] = { 0x00 };
    assert_safe_decode(buf, "all-zeros length 1");
}

TEST_CASE("mpack fuzz: all-zeros buffer length 4", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    const uint8_t buf[4] = {};
    assert_safe_decode(buf, "all-zeros length 4");
}

TEST_CASE("mpack fuzz: all-zeros buffer length 16", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    const uint8_t buf[16] = {};
    assert_safe_decode(buf, "all-zeros length 16");
}

TEST_CASE("mpack fuzz: all-zeros buffer length 100", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    const uint8_t buf[100] = {};
    assert_safe_decode(buf, "all-zeros length 100");
}

TEST_CASE("mpack fuzz: all-0xff buffer length 1", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    const uint8_t buf[] = { 0xff };
    assert_safe_decode(buf, "all-0xff length 1");
}

TEST_CASE("mpack fuzz: all-0xff buffer length 4", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    const uint8_t buf[] = { 0xff, 0xff, 0xff, 0xff };
    assert_safe_decode(buf, "all-0xff length 4");
}

TEST_CASE("mpack fuzz: all-0xff buffer length 16", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    std::vector<uint8_t> buf(16, 0xff);
    assert_safe_decode(buf, "all-0xff length 16");
}

TEST_CASE("mpack fuzz: all-0xff buffer length 100", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    std::vector<uint8_t> buf(100, 0xff);
    assert_safe_decode(buf, "all-0xff length 100");
}

TEST_CASE("mpack fuzz: valid well-formed msgpack value (positive control)", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // Encode a known value and then decode it; both must succeed.
    MpackValue original = NvimRpc::make_array({
        NvimRpc::make_str("redraw"),
        NvimRpc::make_uint(1),
        NvimRpc::make_bool(false),
        NvimRpc::make_nil(),
    });

    std::vector<char> encoded;
    INFO("positive control: encodes successfully");
    REQUIRE(encode_mpack_value(original, encoded));

    MpackValue decoded;
    size_t consumed = 0;
    INFO("positive control: decodes successfully");
    REQUIRE(decode_mpack_value({ reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size() },
        decoded, &consumed));
    INFO("positive control: full buffer consumed");
    REQUIRE(consumed == encoded.size());
    INFO("positive control: type is Array");
    REQUIRE(decoded.type() == MpackValue::Array);
    INFO("positive control: array has 4 elements");
    REQUIRE(static_cast<int>(decoded.as_array().size()) == 4);
    INFO("positive control: first element is string 'redraw'");
    REQUIRE(decoded.as_array()[0].as_str() == std::string("redraw"));
}

TEST_CASE("mpack fuzz: valid value followed by garbage bytes", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0xc3 = true (1 byte), then 0xde 0xad garbage
    const uint8_t buf[] = { 0xc3, 0xde, 0xad };
    MpackValue value;
    size_t consumed = 0;
    bool ok = decode_mpack_value(buf, value, &consumed);
    INFO("trailing garbage: first value decodes successfully");
    REQUIRE(ok);
    INFO("trailing garbage: only the valid byte is consumed");
    REQUIRE(consumed == static_cast<size_t>(1));
}

TEST_CASE("mpack fuzz: fixmap(2) with only one complete pair", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // 0x82 = fixmap(2), then one complete pair: fixstr(1)="a" -> true
    const uint8_t buf[] = {
        0x82, // fixmap(2)
        0xa1,
        0x61, // fixstr(1) = "a"
        0xc3, // true
        // second pair is missing entirely
    };
    assert_safe_decode(buf, "fixmap truncated second pair");
}

TEST_CASE("mpack fuzz: nested array inside map, leaf truncated", "[rpc][slow]")
{
    const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
    if (!run_slow)
        SKIP("slow tests skipped (set DRAXUL_RUN_SLOW_TESTS=1 to enable)");

    // fixmap(1): key="x", value=fixarray(2)[int, <missing>]
    const uint8_t buf[] = {
        0x81, // fixmap(1)
        0xa1,
        0x78, // fixstr(1) = "x"
        0x92, // fixarray(2)
        0x01, // positive fixint 1 (first element)
        // second element is missing
    };
    assert_safe_decode(buf, "nested map/array truncated leaf");
}
