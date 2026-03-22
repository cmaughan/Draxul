#include <catch2/catch_all.hpp>

// Pull in the real production conversion functions from draxul-host.
// Any change to the clipboard algorithm in clipboard_util.h will be caught
// by these tests rather than silently bypassing them.
#include "../libs/draxul-host/src/clipboard_util.h"

#include <draxul/nvim.h>
#include <string>
#include <vector>

using namespace draxul;

namespace
{

// Build a clipboard_set notification params vector [register, lines, regtype].
std::vector<MpackValue> make_clipboard_set_params(const std::string& reg,
    const std::vector<std::string>& lines,
    const std::string& regtype)
{
    std::vector<MpackValue> line_values;
    for (const auto& l : lines)
        line_values.push_back(NvimRpc::make_str(l));

    return {
        NvimRpc::make_str(reg),
        NvimRpc::make_array(std::move(line_values)),
        NvimRpc::make_str(regtype),
    };
}

} // namespace

TEST_CASE("clipboard_set notification stores single-line text", "[nvim]")
{
    auto params = make_clipboard_set_params("+", { "hello world" }, "v");
    std::string text = clipboard_params_to_text(params);
    INFO("single line survives clipboard_set");
    REQUIRE(text == std::string("hello world"));
}

TEST_CASE("clipboard_get returns single-line text as one-element array", "[nvim]")
{
    MpackValue response = clipboard_text_to_response("hello world");
    INFO("response is an array");
    REQUIRE(response.type() == MpackValue::Array);
    const auto& outer = response.as_array();
    INFO("response has two elements");
    REQUIRE(static_cast<int>(outer.size()) == 2);
    INFO("first element is the lines array");
    REQUIRE(outer[0].type() == MpackValue::Array);
    INFO("single line produces one element");
    REQUIRE(static_cast<int>(outer[0].as_array().size()) == 1);
    INFO("line text survives");
    REQUIRE(outer[0].as_array()[0].as_str() == std::string("hello world"));
    INFO("regtype is v");
    REQUIRE(outer[1].as_str() == std::string("v"));
}

TEST_CASE("clipboard round-trip preserves single-line text", "[nvim]")
{
    auto set_params = make_clipboard_set_params("+", { "hello world" }, "v");
    std::string stored = clipboard_params_to_text(set_params);
    MpackValue response = clipboard_text_to_response(stored);
    const auto& lines = response.as_array()[0].as_array();
    INFO("round-trip preserves line count");
    REQUIRE(static_cast<int>(lines.size()) == 1);
    INFO("round-trip preserves text");
    REQUIRE(lines[0].as_str() == std::string("hello world"));
}

TEST_CASE("clipboard round-trip preserves multi-line text", "[nvim]")
{
    auto set_params = make_clipboard_set_params("+", { "line one", "line two", "line three" }, "V");
    std::string stored = clipboard_params_to_text(set_params);
    INFO("lines are joined with newlines");
    REQUIRE(stored == std::string("line one\nline two\nline three"));

    MpackValue response = clipboard_text_to_response(stored);
    const auto& lines = response.as_array()[0].as_array();
    INFO("multi-line round-trip preserves line count");
    REQUIRE(static_cast<int>(lines.size()) == 3);
    INFO("first line survives round-trip");
    REQUIRE(lines[0].as_str() == std::string("line one"));
    INFO("second line survives round-trip");
    REQUIRE(lines[1].as_str() == std::string("line two"));
    INFO("third line survives round-trip");
    REQUIRE(lines[2].as_str() == std::string("line three"));
}

TEST_CASE("clipboard_get on empty text returns one empty-string element", "[nvim]")
{
    MpackValue response = clipboard_text_to_response("");
    const auto& lines = response.as_array()[0].as_array();
    INFO("empty text produces one element");
    REQUIRE(static_cast<int>(lines.size()) == 1);
    INFO("the element is an empty string");
    REQUIRE(lines[0].as_str() == std::string(""));
}

TEST_CASE("clipboard_get before any clipboard_set returns sensible default", "[nvim]")
{
    MpackValue response = clipboard_text_to_response("");
    INFO("response is an array even for empty clipboard");
    REQUIRE(response.type() == MpackValue::Array);
    const auto& outer = response.as_array();
    INFO("response has two elements even for empty clipboard");
    REQUIRE(static_cast<int>(outer.size()) == 2);
    INFO("lines element is an array");
    REQUIRE(outer[0].type() == MpackValue::Array);
    INFO("lines array is never empty");
    REQUIRE(!outer[0].as_array().empty());
}

TEST_CASE("clipboard_set ignores malformed notifications", "[nvim]")
{
    // Too few params.
    std::string text = clipboard_params_to_text({});
    INFO("too few params yields empty string");
    REQUIRE(text == std::string(""));

    // Second param is not an array.
    std::vector<MpackValue> bad_params = {
        NvimRpc::make_str("+"),
        NvimRpc::make_str("not-an-array"),
        NvimRpc::make_str("v"),
    };
    text = clipboard_params_to_text(bad_params);
    INFO("non-array lines param yields empty string");
    REQUIRE(text == std::string(""));
}
