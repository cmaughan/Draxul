#pragma once

#include "lcov_coverage.h"
#include "semantic_city_layout.h"

#include <draxul/perf_timing.h>

#include <cstdint>
#include <string>
#include <vector>

namespace draxul
{

struct LiveCityBuildingMetric
{
    std::string source_file_path;
    std::string module_path;
    std::string qualified_name;
    std::string display_name;
    bool is_struct = false;
    float frame_fraction = 0.0f;
    float smoothed_frame_fraction = 0.0f;
    float heat = 0.0f;
};

struct LiveCityFunctionMetric
{
    std::string source_file_path;
    std::string module_path;
    std::string qualified_name;
    std::string function_name;
    uint32_t layer_index = 0;
    uint32_t layer_count = 0;
    float frame_fraction = 0.0f;
    float smoothed_frame_fraction = 0.0f;
    float heat = 0.0f;
};

struct LiveCityMetricsSnapshot
{
    uint64_t generation = 0;
    std::vector<LiveCityBuildingMetric> buildings;
    std::vector<LiveCityFunctionMetric> functions;
};

struct LiveCityPerfDebugFunction
{
    std::string source_file_path;
    std::string owner_qualified_name;
    std::string function_name;
    float frame_fraction = 0.0f;
    float smoothed_frame_fraction = 0.0f;
    float heat = 0.0f;
};

struct LiveCityPerfDebugState
{
    uint64_t generation = 0;
    uint64_t frame_index = 0;
    uint64_t frame_time_microseconds = 0;
    uint32_t semantic_building_count = 0;
    uint32_t semantic_layer_count = 0;
    uint32_t runtime_function_count = 0;
    uint32_t matched_runtime_function_count = 0;
    uint32_t matched_layer_count = 0;
    uint32_t heated_layer_count = 0;
    uint32_t heated_building_count = 0;
    std::vector<LiveCityPerfDebugFunction> top_matched_functions;
    std::vector<LiveCityPerfDebugFunction> top_unmatched_functions;

    // LCOV import diagnostics (only populated in LcovCoverage mode)
    bool lcov_mode = false;
    uint32_t lcov_report_functions = 0;
    uint32_t lcov_covered_functions = 0;
    uint32_t lcov_matched_layers = 0;
    uint32_t lcov_heated_layers = 0;
    uint32_t lcov_heated_buildings = 0;
};

[[nodiscard]] LiveCityMetricsSnapshot build_live_city_metrics_snapshot(
    const SemanticMegacityModel& model,
    const RuntimePerfSnapshot* perf_snapshot = nullptr,
    bool coverage_mode = false);

[[nodiscard]] LiveCityPerfDebugState build_live_city_perf_debug_state(
    const SemanticMegacityModel& model,
    const RuntimePerfSnapshot* perf_snapshot = nullptr,
    bool coverage_mode = false);

/// Build a metrics snapshot using imported LCOV function coverage.
/// Covered functions get heat 1.0; uncovered get 0.0.
[[nodiscard]] LiveCityMetricsSnapshot build_lcov_city_metrics_snapshot(
    const SemanticMegacityModel& model,
    const LcovFunctionLookup& lcov_lookup);

/// Build debug state for the LCOV overlay mode.
[[nodiscard]] LiveCityPerfDebugState build_lcov_city_perf_debug_state(
    const SemanticMegacityModel& model,
    const LcovFunctionLookup& lcov_lookup);

} // namespace draxul
