#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace draxul
{

struct PerfTimingTag
{
    const char* file = nullptr;
    const char* function = nullptr;
    const char* pretty_function = nullptr;
};

struct RuntimePerfFunctionTiming
{
    std::string source_file_path;
    std::string owner_qualified_name;
    std::string function_name;
    std::string pretty_function;
    uint64_t frame_microseconds = 0;
    uint64_t smoothed_microseconds = 0;
    float frame_fraction = 0.0f;
    float smoothed_frame_fraction = 0.0f;
    float normalized_heat = 0.0f;
    uint32_t call_count = 0;
};

struct RuntimePerfSnapshot
{
    uint64_t generation = 0;
    uint64_t frame_index = 0;
    uint64_t frame_time_microseconds = 0;
    std::vector<RuntimePerfFunctionTiming> functions;
};

class RuntimePerfCollector
{
public:
    void set_enabled(bool enabled);
    [[nodiscard]] bool enabled() const;

    void begin_frame();
    void cancel_frame();
    void end_frame(uint64_t frame_time_override_microseconds = 0);
    void report_timing(const PerfTimingTag& tag, uint64_t microseconds);

    [[nodiscard]] RuntimePerfSnapshot latest_snapshot() const;
    void reset();
};

RuntimePerfCollector& runtime_perf_collector();

class ScopedPerfMeasure
{
public:
    explicit ScopedPerfMeasure(const PerfTimingTag& tag);
    ~ScopedPerfMeasure();

    ScopedPerfMeasure(const ScopedPerfMeasure&) = delete;
    ScopedPerfMeasure& operator=(const ScopedPerfMeasure&) = delete;

private:
    const PerfTimingTag* tag_ = nullptr;
    uint64_t start_microseconds_ = 0;
    bool active_ = false;
};

} // namespace draxul

// Instrumentation policy:
// - Measure lifecycle, frame, I/O, allocation/upload, and meaningful batch work.
// - Do not measure trivial accessors, setters, coordinate helpers, or leaf
//   operations already covered by an owning span; their call and aggregation
//   overhead can exceed the work being observed.
// - Avoid nested spans unless both levels answer distinct diagnostic questions.
// - Keep stable names that are surfaced by diagnostics when moving a span.
//
// Disabled scopes are intentionally a single atomic check. Keep that fast path
// free of clocks, locks, allocation, and string work.

#if defined(_MSC_VER)
#define DRAXUL_PRETTY_FUNCTION __FUNCSIG__
#else
#define DRAXUL_PRETTY_FUNCTION __PRETTY_FUNCTION__
#endif

#define DRAXUL_PERF_CONCAT_IMPL(x, y) x##y
#define DRAXUL_PERF_CONCAT(x, y) DRAXUL_PERF_CONCAT_IMPL(x, y)

#define PERF_MEASURE()                                                                    \
    static const ::draxul::PerfTimingTag DRAXUL_PERF_CONCAT(_draxul_perf_tag_, __LINE__){ \
        __FILE__,                                                                         \
        __func__,                                                                         \
        DRAXUL_PRETTY_FUNCTION,                                                           \
    };                                                                                    \
    ::draxul::ScopedPerfMeasure DRAXUL_PERF_CONCAT(_draxul_perf_scope_, __LINE__)(        \
        DRAXUL_PERF_CONCAT(_draxul_perf_tag_, __LINE__))
