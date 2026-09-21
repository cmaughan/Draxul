#include "support/test_support.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <draxul/nvim.h>
#include <draxul/unicode.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

namespace
{

struct WidthCase
{
    std::string_view label;
    std::string_view text;
    AmbiWidth ambiwidth = AmbiWidth::Single;
};

class NvimWidthOracle
{
public:
    ~NvimWidthOracle()
    {
        shutdown();
    }

    bool initialize()
    {
        if (!process_.spawn())
            return false;
        if (!rpc_.initialize(process_))
        {
            process_.shutdown();
            return false;
        }
        initialized_ = true;
        return true;
    }

    int strdisplaywidth(std::string_view text, AmbiWidth ambiwidth)
    {
        expect(initialized_, "nvim oracle should be initialized");

        auto set_option = rpc_.request("nvim_command", {
                                                           NvimRpc::make_str(std::string("set ambiwidth=") + (ambiwidth == AmbiWidth::Double ? "double" : "single")),
                                                       });
        expect(set_option.has_value(), "nvim should accept the ambiwidth setting");

        auto result = rpc_.request("nvim_call_function", {
                                                             NvimRpc::make_str("strdisplaywidth"),
                                                             NvimRpc::make_array({
                                                                 NvimRpc::make_str(std::string(text)),
                                                             }),
                                                         });

        expect(result.has_value(), "nvim strdisplaywidth should return a display width");
        return (int)result.value().as_int();
    }

private:
    void shutdown()
    {
        if (!initialized_)
            return;
        process_.shutdown();
        rpc_.shutdown();
        initialized_ = false;
    }

    NvimProcess process_;
    NvimRpc rpc_;
    bool initialized_ = false;
};

int local_width(std::string_view text, AmbiWidth ambiwidth)
{
    UiOptions options;
    options.ambiwidth = ambiwidth;
    return cluster_cell_width(text, options);
}

} // namespace

TEST_CASE("unicode decoding and display-width hot paths", "[.unicode-benchmark]")
{
    constexpr std::string_view kCorpus
        = "plain e\xCC\x81 \xE7\x95\x8C \xF0\x9F\x91\xA8\xE2\x80\x8D"
          "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";

    BENCHMARK("decode codepoints")
    {
        size_t offset = 0;
        uint32_t cp = 0;
        uint32_t checksum = 0;
        while (utf8_decode_next(kCorpus, offset, cp))
            checksum ^= cp;
        return checksum;
    };

    BENCHMARK("segment and measure display clusters")
    {
        return display_cell_width(kCorpus);
    };
}

TEST_CASE("unicode helper matches current nvim-like cluster widths", "[unicode]")
{
    REQUIRE(cluster_cell_width("e\xCC\x81") == 1);
    REQUIRE(cluster_cell_width("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD") == 2);
    REQUIRE(cluster_cell_width("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8") == 2);
    REQUIRE(cluster_cell_width("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6") == 2);
    REQUIRE(cluster_cell_width("1\xEF\xB8\x8F\xE2\x83\xA3") == 1);
    REQUIRE(cluster_cell_width("\xE2\x9D\xA4") == 1);
    REQUIRE(cluster_cell_width("\xE2\x9D\xA4\xEF\xB8\x8F") == 2);
    REQUIRE(cluster_cell_width("\xE2\x98\x80") == 1);
    REQUIRE(cluster_cell_width("\xE2\x98\x80\xEF\xB8\x8F") == 2);
    REQUIRE(cluster_cell_width("\xE7\x95\x8C") == 2);
    REQUIRE(cluster_cell_width("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7") == 1);
    REQUIRE(cluster_cell_width("\xF3\xB0\x92\xB2") == 1);
    REQUIRE(cluster_cell_width("\xEE\x98\xA0") == 1);
    UiOptions ambi_double;
    ambi_double.ambiwidth = AmbiWidth::Double;
    REQUIRE(cluster_cell_width("\xCE\xA9", ambi_double) == 2);
    REQUIRE(cluster_cell_width("\xCE\xA9") == 1);
}

TEST_CASE("unicode helper matches headless nvim strdisplaywidth corpus", "[unicode][nvim]")
{
    NvimWidthOracle oracle;
    if (!oracle.initialize())
        SKIP("nvim not available for width conformance test");

    const std::vector<WidthCase> cases = {
        { "combining acute", "e\xCC\x81" },
        { "emoji skin tone", "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD" },
        { "regional indicator flag", "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8" },
        { "family zwj sequence", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6" },
        { "rainbow flag zwj sequence", "\xF0\x9F\x8F\xB3\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x8C\x88" },
        { "keycap sequence", "1\xEF\xB8\x8F\xE2\x83\xA3" },
        { "text heart", "\xE2\x9D\xA4" },
        { "emoji heart", "\xE2\x9D\xA4\xEF\xB8\x8F" },
        { "text tm", "\xE2\x84\xA2" },
        { "emoji tm", "\xE2\x84\xA2\xEF\xB8\x8F" },
        { "text sun", "\xE2\x98\x80" },
        { "emoji sun", "\xE2\x98\x80\xEF\xB8\x8F" },
        { "cjk ideograph", "\xE7\x95\x8C" },
        { "indic conjunct", "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7" },
        { "lazy nerd font icon", "\xF3\xB0\x92\xB2" },
        { "devicon pua icon", "\xEE\x98\xA0" },
        { "ambiguous omega single", "\xCE\xA9", AmbiWidth::Single },
        { "ambiguous omega double", "\xCE\xA9", AmbiWidth::Double },
        { "ambiguous section sign double", "\xC2\xA7", AmbiWidth::Double },
        { "box drawing single", "\xE2\x94\x80", AmbiWidth::Single },
        { "box drawing double", "\xE2\x94\x80", AmbiWidth::Double },
    };

    for (const auto& test_case : cases)
    {
        int local = local_width(test_case.text, test_case.ambiwidth);
        int nvim = oracle.strdisplaywidth(test_case.text, test_case.ambiwidth);
        INFO("width mismatch for " << test_case.label);
        REQUIRE(local == nvim);
    }
}
