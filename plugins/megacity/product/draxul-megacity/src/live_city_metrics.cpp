#include "live_city_metrics.h"

#include <algorithm>
#include <draxul/perf_timing.h>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace draxul
{

namespace
{

struct PerfFunctionLookup
{
    float frame_fraction = 0.0f;
    float smoothed_frame_fraction = 0.0f;
    float heat = 0.0f;
};

std::string perf_owner_function_key(std::string_view owner_qualified_name, std::string_view function_name)
{
    std::string key;
    key.reserve(owner_qualified_name.size() + function_name.size() + 1);
    key.append(owner_qualified_name);
    key.push_back('\n');
    key.append(function_name);
    return key;
}

std::string perf_function_key(
    std::string_view source_file_path,
    std::string_view owner_qualified_name,
    std::string_view function_name)
{
    std::string key;
    key.reserve(source_file_path.size() + owner_qualified_name.size() + function_name.size() + 2);
    key.append(source_file_path);
    key.push_back('\n');
    key.append(owner_qualified_name);
    key.push_back('\n');
    key.append(function_name);
    return key;
}

std::string short_qualified_name(std::string_view qualified_name)
{
    const size_t last_scope = qualified_name.rfind("::");
    if (last_scope == std::string_view::npos)
        return std::string(qualified_name);
    return std::string(qualified_name.substr(last_scope + 2));
}

float display_heat_for_timing(const RuntimePerfFunctionTiming& timing, bool coverage_mode)
{
    if (!coverage_mode)
        return timing.normalized_heat;
    return (timing.frame_fraction > 0.0f
               || timing.smoothed_frame_fraction > 0.0f
               || timing.frame_microseconds > 0
               || timing.smoothed_microseconds > 0
               || timing.call_count > 0)
        ? 1.0f
        : 0.0f;
}

void accumulate_perf_function(
    std::unordered_map<std::string, PerfFunctionLookup>& lookup,
    std::string_view source_file_path,
    std::string_view owner_qualified_name,
    std::string_view function_name,
    const RuntimePerfFunctionTiming& timing,
    bool coverage_mode)
{
    PERF_MEASURE();
    if (source_file_path.empty() || owner_qualified_name.empty() || function_name.empty())
        return;

    PerfFunctionLookup& entry
        = lookup[perf_function_key(source_file_path, owner_qualified_name, function_name)];
    entry.frame_fraction = std::max(entry.frame_fraction, timing.frame_fraction);
    entry.smoothed_frame_fraction = std::max(entry.smoothed_frame_fraction, timing.smoothed_frame_fraction);
    entry.heat = std::max(entry.heat, display_heat_for_timing(timing, coverage_mode));
}

void accumulate_perf_function_without_file(
    std::unordered_map<std::string, PerfFunctionLookup>& lookup,
    std::string_view owner_qualified_name,
    std::string_view function_name,
    const RuntimePerfFunctionTiming& timing,
    bool coverage_mode)
{
    PERF_MEASURE();
    if (owner_qualified_name.empty() || function_name.empty())
        return;

    PerfFunctionLookup& entry = lookup[perf_owner_function_key(owner_qualified_name, function_name)];
    entry.frame_fraction = std::max(entry.frame_fraction, timing.frame_fraction);
    entry.smoothed_frame_fraction = std::max(entry.smoothed_frame_fraction, timing.smoothed_frame_fraction);
    entry.heat = std::max(entry.heat, display_heat_for_timing(timing, coverage_mode));
}

struct PerfLookupSet
{
    std::unordered_map<std::string, PerfFunctionLookup> by_file;
    std::unordered_map<std::string, PerfFunctionLookup> by_owner_function;
};

PerfLookupSet build_perf_lookup(const RuntimePerfSnapshot* perf_snapshot, bool coverage_mode)
{
    PERF_MEASURE();
    PerfLookupSet lookup;
    if (!perf_snapshot)
        return lookup;

    lookup.by_file.reserve(perf_snapshot->functions.size() * 2u);
    lookup.by_owner_function.reserve(perf_snapshot->functions.size() * 2u);
    for (const RuntimePerfFunctionTiming& timing : perf_snapshot->functions)
    {
        if (timing.source_file_path.empty() || timing.function_name.empty())
            continue;

        accumulate_perf_function(
            lookup.by_file,
            timing.source_file_path,
            timing.owner_qualified_name,
            timing.function_name,
            timing,
            coverage_mode);
        accumulate_perf_function_without_file(
            lookup.by_owner_function,
            timing.owner_qualified_name,
            timing.function_name,
            timing,
            coverage_mode);

        const std::string owner_short_name = short_qualified_name(timing.owner_qualified_name);
        if (owner_short_name != timing.owner_qualified_name)
        {
            accumulate_perf_function(
                lookup.by_file,
                timing.source_file_path,
                owner_short_name,
                timing.function_name,
                timing,
                coverage_mode);
            accumulate_perf_function_without_file(
                lookup.by_owner_function,
                owner_short_name,
                timing.function_name,
                timing,
                coverage_mode);
        }
    }

    return lookup;
}

PerfFunctionLookup find_perf_function(
    const PerfLookupSet& lookup,
    std::string_view source_file_path,
    std::string_view owner_qualified_name,
    std::string_view function_name)
{
    PERF_MEASURE();
    const auto exact_it = lookup.by_file.find(perf_function_key(source_file_path, owner_qualified_name, function_name));
    if (exact_it != lookup.by_file.end())
        return exact_it->second;

    const auto owner_it = lookup.by_owner_function.find(perf_owner_function_key(owner_qualified_name, function_name));
    if (owner_it != lookup.by_owner_function.end())
        return owner_it->second;

    return {};
}

bool perf_lookup_contains(
    const PerfLookupSet& lookup,
    std::string_view source_file_path,
    std::string_view owner_qualified_name,
    std::string_view function_name)
{
    return lookup.by_file.contains(perf_function_key(source_file_path, owner_qualified_name, function_name))
        || lookup.by_owner_function.contains(perf_owner_function_key(owner_qualified_name, function_name));
}

bool has_non_zero_perf(const PerfFunctionLookup& perf)
{
    return perf.frame_fraction > 0.0f
        || perf.smoothed_frame_fraction > 0.0f
        || perf.heat > 0.0f;
}

LiveCityPerfDebugFunction make_debug_function(const RuntimePerfFunctionTiming& timing, bool coverage_mode)
{
    return {
        .source_file_path = timing.source_file_path,
        .owner_qualified_name = timing.owner_qualified_name,
        .function_name = timing.function_name,
        .frame_fraction = timing.frame_fraction,
        .smoothed_frame_fraction = timing.smoothed_frame_fraction,
        .heat = display_heat_for_timing(timing, coverage_mode),
    };
}

void sort_debug_functions(std::vector<LiveCityPerfDebugFunction>& functions)
{
    PERF_MEASURE();
    std::sort(
        functions.begin(),
        functions.end(),
        [](const LiveCityPerfDebugFunction& lhs, const LiveCityPerfDebugFunction& rhs) {
            if (lhs.heat != rhs.heat)
                return lhs.heat > rhs.heat;
            if (lhs.smoothed_frame_fraction != rhs.smoothed_frame_fraction)
                return lhs.smoothed_frame_fraction > rhs.smoothed_frame_fraction;
            if (lhs.owner_qualified_name != rhs.owner_qualified_name)
                return lhs.owner_qualified_name < rhs.owner_qualified_name;
            return lhs.function_name < rhs.function_name;
        });
    constexpr size_t kMaxEntries = 10;
    if (functions.size() > kMaxEntries)
        functions.resize(kMaxEntries);
}

} // namespace

LiveCityMetricsSnapshot build_live_city_metrics_snapshot(
    const SemanticMegacityModel& model,
    const RuntimePerfSnapshot* perf_snapshot,
    bool coverage_mode)
{
    PERF_MEASURE();
    LiveCityMetricsSnapshot snapshot;
    snapshot.generation = perf_snapshot ? perf_snapshot->generation : 0;
    const auto perf_lookup = build_perf_lookup(perf_snapshot, coverage_mode);

    for (const auto& mod : model.modules)
    {
        snapshot.buildings.reserve(snapshot.buildings.size() + mod.buildings.size());
        for (const auto& building : mod.buildings)
        {
            float building_frame_fraction = 0.0f;
            float building_smoothed_fraction = 0.0f;
            float building_heat = 0.0f;

            if (!building.layers.empty())
            {
                for (size_t layer_index = 0; layer_index < building.layers.size(); ++layer_index)
                {
                    const SemanticBuildingLayer& layer = building.layers[layer_index];
                    const PerfFunctionLookup perf = find_perf_function(
                        perf_lookup,
                        building.source_file_path,
                        building.qualified_name,
                        layer.function_name);
                    snapshot.functions.push_back({
                        .source_file_path = building.source_file_path,
                        .module_path = building.module_path,
                        .qualified_name = building.qualified_name,
                        .function_name = layer.function_name,
                        .layer_index = static_cast<uint32_t>(layer_index),
                        .layer_count = static_cast<uint32_t>(building.layers.size()),
                        .frame_fraction = perf.frame_fraction,
                        .smoothed_frame_fraction = perf.smoothed_frame_fraction,
                        .heat = perf.heat,
                    });
                    building_frame_fraction = std::max(building_frame_fraction, perf.frame_fraction);
                    building_smoothed_fraction = std::max(building_smoothed_fraction, perf.smoothed_frame_fraction);
                    building_heat = std::max(building_heat, perf.heat);
                }
            }

            snapshot.buildings.push_back({
                .source_file_path = building.source_file_path,
                .module_path = building.module_path,
                .qualified_name = building.qualified_name,
                .display_name = building.display_name.empty() ? building.qualified_name : building.display_name,
                .is_struct = building.is_struct,
                .frame_fraction = building_frame_fraction,
                .smoothed_frame_fraction = building_smoothed_fraction,
                .heat = building_heat,
            });
        }
    }

    return snapshot;
}

LiveCityPerfDebugState build_live_city_perf_debug_state(
    const SemanticMegacityModel& model,
    const RuntimePerfSnapshot* perf_snapshot,
    bool coverage_mode)
{
    PERF_MEASURE();
    LiveCityPerfDebugState debug;
    if (!perf_snapshot)
    {
        for (const auto& mod : model.modules)
        {
            debug.semantic_building_count += static_cast<uint32_t>(mod.buildings.size());
            for (const auto& building : mod.buildings)
                debug.semantic_layer_count += static_cast<uint32_t>(building.layers.size());
        }
        return debug;
    }

    debug.generation = perf_snapshot->generation;
    debug.frame_index = perf_snapshot->frame_index;
    debug.frame_time_microseconds = perf_snapshot->frame_time_microseconds;
    debug.runtime_function_count = static_cast<uint32_t>(perf_snapshot->functions.size());

    const auto perf_lookup = build_perf_lookup(perf_snapshot, coverage_mode);
    std::unordered_set<std::string> matched_exact_runtime_keys;
    std::unordered_set<std::string> matched_owner_runtime_keys;

    for (const auto& mod : model.modules)
    {
        debug.semantic_building_count += static_cast<uint32_t>(mod.buildings.size());
        for (const auto& building : mod.buildings)
        {
            bool building_heated = false;
            debug.semantic_layer_count += static_cast<uint32_t>(building.layers.size());
            for (const auto& layer : building.layers)
            {
                const PerfFunctionLookup perf = find_perf_function(
                    perf_lookup,
                    building.source_file_path,
                    building.qualified_name,
                    layer.function_name);
                if (perf_lookup_contains(
                        perf_lookup,
                        building.source_file_path,
                        building.qualified_name,
                        layer.function_name))
                {
                    ++debug.matched_layer_count;
                    matched_exact_runtime_keys.insert(
                        perf_function_key(building.source_file_path, building.qualified_name, layer.function_name));
                    matched_owner_runtime_keys.insert(
                        perf_owner_function_key(building.qualified_name, layer.function_name));
                }
                if (has_non_zero_perf(perf))
                {
                    ++debug.heated_layer_count;
                    building_heated = true;
                }
            }
            if (building_heated)
                ++debug.heated_building_count;
        }
    }

    for (const RuntimePerfFunctionTiming& timing : perf_snapshot->functions)
    {
        const bool matched = matched_exact_runtime_keys.contains(
                                 perf_function_key(timing.source_file_path, timing.owner_qualified_name, timing.function_name))
            || matched_owner_runtime_keys.contains(
                perf_owner_function_key(timing.owner_qualified_name, timing.function_name))
            || (!timing.owner_qualified_name.empty()
                && matched_owner_runtime_keys.contains(
                    perf_owner_function_key(short_qualified_name(timing.owner_qualified_name), timing.function_name)));

        if (matched)
        {
            ++debug.matched_runtime_function_count;
            debug.top_matched_functions.push_back(make_debug_function(timing, coverage_mode));
        }
        else
        {
            debug.top_unmatched_functions.push_back(make_debug_function(timing, coverage_mode));
        }
    }

    sort_debug_functions(debug.top_matched_functions);
    sort_debug_functions(debug.top_unmatched_functions);
    return debug;
}

LiveCityMetricsSnapshot build_lcov_city_metrics_snapshot(
    const SemanticMegacityModel& model,
    const LcovFunctionLookup& lcov_lookup)
{
    PERF_MEASURE();
    LiveCityMetricsSnapshot snapshot;
    snapshot.generation = 1; // static import — single generation

    for (const auto& mod : model.modules)
    {
        snapshot.buildings.reserve(snapshot.buildings.size() + mod.buildings.size());
        for (const auto& building : mod.buildings)
        {
            float building_heat = 0.0f;

            if (!building.layers.empty())
            {
                for (size_t layer_index = 0; layer_index < building.layers.size(); ++layer_index)
                {
                    const SemanticBuildingLayer& layer = building.layers[layer_index];
                    const bool covered = lcov_function_covered(
                        lcov_lookup,
                        building.source_file_path,
                        building.qualified_name,
                        layer.function_name);
                    const float heat = covered ? 1.0f : 0.0f;
                    snapshot.functions.push_back({
                        .source_file_path = building.source_file_path,
                        .module_path = building.module_path,
                        .qualified_name = building.qualified_name,
                        .function_name = layer.function_name,
                        .layer_index = static_cast<uint32_t>(layer_index),
                        .layer_count = static_cast<uint32_t>(building.layers.size()),
                        .frame_fraction = 0.0f,
                        .smoothed_frame_fraction = 0.0f,
                        .heat = heat,
                    });
                    building_heat = std::max(building_heat, heat);
                }
            }

            snapshot.buildings.push_back({
                .source_file_path = building.source_file_path,
                .module_path = building.module_path,
                .qualified_name = building.qualified_name,
                .display_name = building.display_name.empty() ? building.qualified_name : building.display_name,
                .is_struct = building.is_struct,
                .frame_fraction = 0.0f,
                .smoothed_frame_fraction = 0.0f,
                .heat = building_heat,
            });
        }
    }

    return snapshot;
}

LiveCityPerfDebugState build_lcov_city_perf_debug_state(
    const SemanticMegacityModel& model,
    const LcovFunctionLookup& lcov_lookup)
{
    PERF_MEASURE();
    LiveCityPerfDebugState debug;
    debug.lcov_mode = true;
    debug.lcov_report_functions = lcov_lookup.total_report_functions;
    debug.lcov_covered_functions = lcov_lookup.covered_report_functions;

    for (const auto& mod : model.modules)
    {
        debug.semantic_building_count += static_cast<uint32_t>(mod.buildings.size());
        for (const auto& building : mod.buildings)
        {
            bool building_heated = false;
            debug.semantic_layer_count += static_cast<uint32_t>(building.layers.size());
            for (const auto& layer : building.layers)
            {
                const bool covered = lcov_function_covered(
                    lcov_lookup,
                    building.source_file_path,
                    building.qualified_name,
                    layer.function_name);
                if (covered)
                {
                    ++debug.lcov_matched_layers;
                    ++debug.lcov_heated_layers;
                    building_heated = true;
                }
            }
            if (building_heated)
                ++debug.lcov_heated_buildings;
        }
    }

    return debug;
}

} // namespace draxul
