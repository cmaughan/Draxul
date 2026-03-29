#pragma once

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
};

[[nodiscard]] LiveCityMetricsSnapshot build_live_city_metrics_snapshot(
    const SemanticMegacityModel& model,
    const RuntimePerfSnapshot* perf_snapshot = nullptr);

[[nodiscard]] LiveCityPerfDebugState build_live_city_perf_debug_state(
    const SemanticMegacityModel& model,
    const RuntimePerfSnapshot* perf_snapshot = nullptr);

} // namespace draxul
