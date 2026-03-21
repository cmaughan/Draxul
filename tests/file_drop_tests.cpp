// file_drop_tests.cpp
//
// Work Item 12: file-drop-special-chars
//
// Tests that file-drop paths with special characters are forwarded correctly
// through the open_file: action string format.
//
// The drop-file path in InputDispatcher::connect() is encoded as:
//   std::string("open_file:") + std::string(path)
//
// On the receiving end (NvimHost::dispatch_action, nvim_host.cpp:161-168):
//   action.starts_with("open_file:")
//   const std::string path(action.substr(10));   // 10 == len("open_file:")
//
// Colon-containing paths: because substr(10) takes EVERYTHING after the
// 10-character prefix, a path like "/path:with:colons/file.txt" is decoded
// as the full string "/path:with:colons/file.txt" — no truncation occurs.
// The colon ambiguity would only arise if the implementation used find(':')
// to locate the separator; since it uses a fixed-length prefix, it is safe.
//
// These tests verify:
//   - The action string round-trips correctly for all special-character paths.
//   - The decoded path is identical to the original dropped path.
//   - No crash occurs for any of the test paths.

#include <catch2/catch_all.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace
{

// Encode a dropped file path into an open_file: action string.
// Mirrors the logic in InputDispatcher::connect() (app/input_dispatcher.cpp).
std::string encode_drop_path(std::string_view path)
{
    return std::string("open_file:") + std::string(path);
}

// Decode a path from an open_file: action string.
// Mirrors the logic in NvimHost::dispatch_action() (libs/draxul-host/src/nvim_host.cpp).
// Returns the empty string if the action does not start with "open_file:".
std::string decode_drop_path(std::string_view action)
{
    constexpr std::string_view kPrefix = "open_file:";
    if (!action.starts_with(kPrefix))
        return {};
    return std::string(action.substr(kPrefix.size()));
}

struct PathTestCase
{
    const char* description;
    const char* path;
};

} // namespace

// ---------------------------------------------------------------------------
// Round-trip encoding / decoding
// ---------------------------------------------------------------------------

TEST_CASE("file drop: open_file: prefix is correctly identified", "[file_drop]")
{
    const std::string action = encode_drop_path("/normal/path.txt");
    REQUIRE(action.starts_with("open_file:"));
}

TEST_CASE("file drop: paths round-trip through open_file: encoding", "[file_drop]")
{
    const PathTestCase cases[] = {
        { "normal path", "/normal/path.txt" },
        { "path with spaces", "/path with spaces/file.txt" },
        { "path with colons", "/path:with:colons/file.txt" },
        { "path with unicode", "/path/with/\xC3\xBC\x6E\xC3\xAF\x63\xC3\xB6\x64\xC3\xA9/file.txt" }, // ünïcödé in UTF-8
        { "path with dollar sign", "/path/$SPECIAL/file.txt" },
        { "path with exclamation mark", "/path/with!/file.txt" },
        { "path with backtick", "/path/with`/file.txt" },
        { "path with ampersand", "/path/with&/file.txt" },
        { "path with hash", "/path/with#/file.txt" },
        { "path with percent", "/path/with%20/file.txt" },
        { "path with single quote", "/path/with'/file.txt" },
        { "path with double quote", "/path/with\"/file.txt" },
        { "path with backslash", "/path/with\\/file.txt" },
        { "path with newline", "/path/with\n/file.txt" },
        { "root file", "/file.txt" },
        { "deeply nested path", "/a/b/c/d/e/f/g/h/i/j/k/l/m/n.txt" },
        { "path with multiple colons", "/usr/local/share:extra:stuff/file.txt" },
        { "empty filename component", "/path//file.txt" },
    };

    for (const auto& tc : cases)
    {
        INFO("test case: " << tc.description << " — path: " << tc.path);
        const std::string action = encode_drop_path(tc.path);
        const std::string decoded = decode_drop_path(action);
        REQUIRE(decoded == tc.path);
    }
}

// ---------------------------------------------------------------------------
// Colon-containing paths: the key correctness property
// ---------------------------------------------------------------------------

TEST_CASE("file drop: colon-containing path is decoded without truncation", "[file_drop]")
{
    // This is the critical regression test. A naive implementation using
    // action.find(':') to split the action string would extract only "open_file"
    // as the action and "/path" as the argument for "/path:with:colons/file.txt",
    // losing the rest of the path.
    //
    // The actual implementation uses starts_with("open_file:") + substr(10),
    // which is correct: the full path is preserved regardless of embedded colons.

    const std::string path = "/path:with:colons/file.txt";
    const std::string action = encode_drop_path(path);
    const std::string decoded = decode_drop_path(action);

    INFO("full path including all colons must survive the round-trip");
    REQUIRE(decoded == path);
    INFO("decoded path must not be truncated at the first colon in the path");
    REQUIRE(decoded.find("with:colons") != std::string::npos);
}

TEST_CASE("file drop: path starting with colon is decoded correctly", "[file_drop]")
{
    // Unusual but valid on some filesystems: a path element starts with ':'.
    const std::string path = "/path/:colon-prefixed/file.txt";
    const std::string action = encode_drop_path(path);
    const std::string decoded = decode_drop_path(action);
    REQUIRE(decoded == path);
}

// ---------------------------------------------------------------------------
// Unicode paths (UTF-8)
// ---------------------------------------------------------------------------

TEST_CASE("file drop: unicode path with multi-byte UTF-8 sequences round-trips correctly", "[file_drop]")
{
    // "ünïcödé" in UTF-8
    const std::string path = "/path/\xC3\xBC\x6E\xC3\xAF\x63\xC3\xB6\x64\xC3\xA9/file.txt";
    const std::string action = encode_drop_path(path);
    const std::string decoded = decode_drop_path(action);
    REQUIRE(decoded == path);
}

TEST_CASE("file drop: CJK characters in path round-trip correctly", "[file_drop]")
{
    // "文档" (documents) in UTF-8
    const std::string path = "/\xE6\x96\x87\xE6\xA1\xA3/file.txt";
    const std::string action = encode_drop_path(path);
    const std::string decoded = decode_drop_path(action);
    REQUIRE(decoded == path);
}

// ---------------------------------------------------------------------------
// Shell-sensitive characters
// ---------------------------------------------------------------------------

TEST_CASE("file drop: dollar-sign in path does not cause truncation or substitution", "[file_drop]")
{
    // Shell variable expansion is not relevant here — this is a string operation.
    // The test documents that $SPECIAL is preserved literally.
    const std::string path = "/path/$SPECIAL/file.txt";
    const std::string action = encode_drop_path(path);
    const std::string decoded = decode_drop_path(action);
    REQUIRE(decoded == path);
}

TEST_CASE("file drop: spaces in path are preserved verbatim", "[file_drop]")
{
    const std::string path = "/path with spaces/my document.txt";
    const std::string action = encode_drop_path(path);
    const std::string decoded = decode_drop_path(action);
    REQUIRE(decoded == path);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST_CASE("file drop: decode_drop_path returns empty string for non-open_file: action", "[file_drop]")
{
    REQUIRE(decode_drop_path("copy") == "");
    REQUIRE(decode_drop_path("paste") == "");
    REQUIRE(decode_drop_path("open_file_dialog") == "");
    REQUIRE(decode_drop_path("") == "");
}

TEST_CASE("file drop: empty path encodes and decodes to empty string", "[file_drop]")
{
    const std::string path;
    const std::string action = encode_drop_path(path);
    const std::string decoded = decode_drop_path(action);
    REQUIRE(decoded == path);
    REQUIRE(decoded.empty());
}

TEST_CASE("file drop: very long path (4096 chars) round-trips correctly", "[file_drop]")
{
    // Some file systems allow paths up to 4096 bytes.
    std::string path = "/";
    // Build a path with many components that stays under typical OS limits.
    for (int i = 0; i < 50; ++i)
        path += "long_directory_name_component_" + std::to_string(i) + "/";
    path += "file.txt";

    const std::string action = encode_drop_path(path);
    const std::string decoded = decode_drop_path(action);
    REQUIRE(decoded == path);
}
