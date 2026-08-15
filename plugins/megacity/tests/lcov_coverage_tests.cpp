#include <catch2/catch_all.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

#include "lcov_coverage.h"
#include "live_city_metrics.h"
#include "semantic_city_layout.h"

using namespace draxul;

// ---------------------------------------------------------------------------
// LCOV parsing
// ---------------------------------------------------------------------------

TEST_CASE("parse_lcov: empty input", "[lcov]")
{
    const auto report = parse_lcov("");
    CHECK(report.files.empty());
    CHECK(report.total_functions == 0);
    CHECK(report.covered_functions == 0);
}

TEST_CASE("parse_lcov: single file with functions", "[lcov]")
{
    const char* lcov_data = "SF:/repo/libs/foo/src/bar.cpp\n"
                            "FN:10,draxul::Bar::do_thing(int)\n"
                            "FN:25,draxul::Bar::other()\n"
                            "FNDA:5,draxul::Bar::do_thing(int)\n"
                            "FNDA:0,draxul::Bar::other()\n"
                            "end_of_record\n";

    const auto report = parse_lcov(lcov_data);
    REQUIRE(report.files.size() == 1);
    CHECK(report.files[0].source_file == "/repo/libs/foo/src/bar.cpp");
    REQUIRE(report.files[0].functions.size() == 2);

    CHECK(report.files[0].functions[0].function_name == "draxul::Bar::do_thing(int)");
    CHECK(report.files[0].functions[0].hit_count == 5);
    CHECK(report.files[0].functions[1].function_name == "draxul::Bar::other()");
    CHECK(report.files[0].functions[1].hit_count == 0);

    CHECK(report.total_functions == 2);
    CHECK(report.covered_functions == 1);
}

TEST_CASE("parse_lcov: multiple files", "[lcov]")
{
    const char* lcov_data = "SF:/repo/a.cpp\n"
                            "FN:1,foo()\n"
                            "FNDA:3,foo()\n"
                            "end_of_record\n"
                            "SF:/repo/b.cpp\n"
                            "FN:1,bar()\n"
                            "FNDA:0,bar()\n"
                            "end_of_record\n";

    const auto report = parse_lcov(lcov_data);
    CHECK(report.files.size() == 2);
    CHECK(report.total_functions == 2);
    CHECK(report.covered_functions == 1);
}

TEST_CASE("parse_lcov: handles CRLF line endings", "[lcov]")
{
    const char* lcov_data = "SF:/repo/a.cpp\r\n"
                            "FN:1,foo()\r\n"
                            "FNDA:1,foo()\r\n"
                            "end_of_record\r\n";

    const auto report = parse_lcov(lcov_data);
    REQUIRE(report.files.size() == 1);
    CHECK(report.files[0].source_file == "/repo/a.cpp");
    CHECK(report.files[0].functions[0].hit_count == 1);
}

// ---------------------------------------------------------------------------
// LCOV lookup construction and matching
// ---------------------------------------------------------------------------

TEST_CASE("build_lcov_lookup: relative path matching", "[lcov]")
{
    LcovCoverageReport report;
    report.files.push_back({
        "/Users/dev/repo/libs/foo/src/bar.cpp",
        { { "draxul::Bar::do_thing(int)", 5 }, { "draxul::Bar::other()", 0 } },
    });
    report.total_functions = 2;
    report.covered_functions = 1;

    const auto lookup = build_lcov_lookup(report, "/Users/dev/repo");
    CHECK(lookup.total_report_functions == 2);
    CHECK(lookup.covered_report_functions == 1);

    // Exact file + short function name match
    CHECK(lcov_function_covered(lookup, "libs/foo/src/bar.cpp", "draxul::Bar", "do_thing"));
    CHECK_FALSE(lcov_function_covered(lookup, "libs/foo/src/bar.cpp", "draxul::Bar", "other"));

    // Function-only fallback
    CHECK(lcov_function_covered(lookup, "wrong/path.cpp", "", "do_thing"));
    CHECK_FALSE(lcov_function_covered(lookup, "wrong/path.cpp", "", "other"));
}

TEST_CASE("build_lcov_lookup: function-only fallback uses max hit count", "[lcov]")
{
    LcovCoverageReport report;
    report.files.push_back({ "/repo/a.cpp", { { "foo()", 0 } } });
    report.files.push_back({ "/repo/b.cpp", { { "foo()", 3 } } });
    report.total_functions = 2;
    report.covered_functions = 1;

    const auto lookup = build_lcov_lookup(report, "/repo");

    // Function-only fallback should find the covered version
    CHECK(lcov_function_covered(lookup, "unknown.cpp", "", "foo"));
}

// ---------------------------------------------------------------------------
// LCOV metrics snapshot integration
// ---------------------------------------------------------------------------

namespace
{

SemanticMegacityModel make_test_model()
{
    SemanticMegacityModel model;
    SemanticCityModuleModel module;
    module.module_path = "libs/foo";

    SemanticCityBuilding building;
    building.source_file_path = "libs/foo/src/bar.cpp";
    building.module_path = "libs/foo";
    building.qualified_name = "draxul::Bar";
    building.display_name = "Bar";
    building.layers = {
        { "do_thing", "", 10, 1.0f },
        { "other", "", 5, 1.0f },
        { "unused", "", 3, 1.0f },
    };
    module.buildings.push_back(std::move(building));
    model.modules.push_back(std::move(module));
    return model;
}

LcovFunctionLookup make_test_lookup()
{
    LcovCoverageReport report;
    report.files.push_back({
        "/repo/libs/foo/src/bar.cpp",
        { { "draxul::Bar::do_thing(int)", 5 }, { "draxul::Bar::other()", 0 } },
    });
    report.total_functions = 2;
    report.covered_functions = 1;
    return build_lcov_lookup(report, "/repo");
}

} // namespace

TEST_CASE("build_lcov_city_metrics_snapshot: covered functions get heat 1.0", "[lcov]")
{
    const auto model = make_test_model();
    const auto lookup = make_test_lookup();
    const auto snapshot = build_lcov_city_metrics_snapshot(model, lookup);

    REQUIRE(snapshot.buildings.size() == 1);
    CHECK(snapshot.buildings[0].heat > 0.0f); // at least one layer covered

    REQUIRE(snapshot.functions.size() == 3);
    CHECK(snapshot.functions[0].function_name == "do_thing");
    CHECK(snapshot.functions[0].heat == 1.0f); // covered
    CHECK(snapshot.functions[1].function_name == "other");
    CHECK(snapshot.functions[1].heat == 0.0f); // not covered (0 hits)
    CHECK(snapshot.functions[2].function_name == "unused");
    CHECK(snapshot.functions[2].heat == 0.0f); // not in report
}

TEST_CASE("build_lcov_city_perf_debug_state: reports correct counts", "[lcov]")
{
    const auto model = make_test_model();
    const auto lookup = make_test_lookup();
    const auto debug = build_lcov_city_perf_debug_state(model, lookup);

    CHECK(debug.lcov_mode);
    CHECK(debug.lcov_report_functions == 2);
    CHECK(debug.lcov_covered_functions == 1);
    CHECK(debug.semantic_building_count == 1);
    CHECK(debug.semantic_layer_count == 3);
    CHECK(debug.lcov_matched_layers == 1); // only "do_thing" is covered
    CHECK(debug.lcov_heated_layers == 1);
    CHECK(debug.lcov_heated_buildings == 1);
}

#endif // DRAXUL_ENABLE_MEGACITY
