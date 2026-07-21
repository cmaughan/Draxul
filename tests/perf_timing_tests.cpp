#include <catch2/catch_all.hpp>

#include <draxul/perf_timing.h>

#include <chrono>
#include <mutex>

using namespace draxul;

namespace
{

const RuntimePerfFunctionTiming* find_perf_function(
    const RuntimePerfSnapshot& snapshot,
    std::string_view owner_qualified_name,
    std::string_view function_name)
{
    for (const RuntimePerfFunctionTiming& function : snapshot.functions)
    {
        if (function.owner_qualified_name == owner_qualified_name
            && function.function_name == function_name)
        {
            return &function;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("runtime perf collector reports normalized smoothed frame timings", "[perf]")
{
    auto& collector = runtime_perf_collector();
    collector.reset();
    collector.set_enabled(true);

    static const PerfTimingTag kBeginFrameTag{
        "/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/metal/metal_renderer.mm",
        "begin_frame",
        "bool draxul::MetalRenderer::begin_frame()",
    };
    static const PerfTimingTag kEndFrameTag{
        "/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/metal/metal_renderer.mm",
        "end_frame",
        "void draxul::MetalRenderer::end_frame()",
    };

    collector.begin_frame();
    collector.report_timing(kBeginFrameTag, 2000);
    collector.report_timing(kEndFrameTag, 1000);
    collector.end_frame(3000);

    const RuntimePerfSnapshot snapshot = collector.latest_snapshot();
    REQUIRE(snapshot.generation == 1);
    REQUIRE(snapshot.frame_index == 1);
    REQUIRE(snapshot.frame_time_microseconds == 3000);
    REQUIRE(snapshot.functions.size() == 2);

    const RuntimePerfFunctionTiming* begin_frame = find_perf_function(snapshot, "MetalRenderer", "begin_frame");
    REQUIRE(begin_frame != nullptr);
    CHECK(begin_frame->source_file_path == "libs/draxul-renderer/src/metal/metal_renderer.mm");
    CHECK(begin_frame->frame_microseconds == 2000);
    CHECK(begin_frame->call_count == 1u);
    CHECK(begin_frame->frame_fraction == Catch::Approx(2.0f / 3.0f));
    CHECK(begin_frame->normalized_heat == Catch::Approx(1.0f));

    const RuntimePerfFunctionTiming* end_frame = find_perf_function(snapshot, "MetalRenderer", "end_frame");
    REQUIRE(end_frame != nullptr);
    CHECK(end_frame->frame_microseconds == 1000);
    CHECK(end_frame->call_count == 1u);
    CHECK(end_frame->frame_fraction == Catch::Approx(1.0f / 3.0f));
    CHECK(end_frame->normalized_heat < begin_frame->normalized_heat);

    collector.reset();
    collector.set_enabled(false);
}

TEST_CASE("runtime perf scope overhead benchmark", "[.perf-benchmark]")
{
    auto& collector = runtime_perf_collector();
    collector.reset();

    static const PerfTimingTag kBenchmarkTag{
        "tests/perf_timing_tests.cpp",
        "perf_scope_benchmark",
        "void draxul::perf_scope_benchmark()",
    };

    collector.set_enabled(false);
    std::mutex legacy_mutex;
    bool legacy_enabled = false;
    BENCHMARK("legacy disabled scope shape")
    {
        const auto start = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(legacy_mutex);
        return legacy_enabled
            ? std::chrono::steady_clock::now() - start
            : std::chrono::steady_clock::duration::zero();
    };

    BENCHMARK("disabled scoped measurement")
    {
        ScopedPerfMeasure measurement(kBenchmarkTag);
        return 0;
    };

    collector.set_enabled(true);
    collector.begin_frame();
    BENCHMARK("enabled scoped measurement")
    {
        ScopedPerfMeasure measurement(kBenchmarkTag);
        return 0;
    };
    collector.cancel_frame();
    collector.set_enabled(false);
}
