#include "metrics_overlay_controller.h"

#include "semantic_city_layout.h"

#include <draxul/log.h>
#include <filesystem>
#include <unordered_map>

namespace draxul
{
namespace
{
constexpr auto kLivePerfRefreshTick = std::chrono::milliseconds(100);

bool runtime_timing_has_activity(const RuntimePerfFunctionTiming& timing)
{
    return timing.frame_fraction > 0.0f
        || timing.smoothed_frame_fraction > 0.0f
        || timing.frame_microseconds > 0
        || timing.smoothed_microseconds > 0
        || timing.call_count > 0;
}

std::string runtime_perf_function_key(
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

void clear_coverage_snapshot(RuntimePerfSnapshot& snapshot)
{
    snapshot = RuntimePerfSnapshot{};
}

void merge_runtime_perf_coverage(RuntimePerfSnapshot& coverage_snapshot, const RuntimePerfSnapshot& latest_snapshot)
{
    coverage_snapshot.generation = latest_snapshot.generation;
    coverage_snapshot.frame_index = latest_snapshot.frame_index;
    coverage_snapshot.frame_time_microseconds = latest_snapshot.frame_time_microseconds;

    std::unordered_map<std::string, size_t> indices_by_key;
    indices_by_key.reserve(coverage_snapshot.functions.size());
    for (size_t i = 0; i < coverage_snapshot.functions.size(); ++i)
    {
        const auto& function = coverage_snapshot.functions[i];
        indices_by_key.emplace(
            runtime_perf_function_key(
                function.source_file_path,
                function.owner_qualified_name,
                function.function_name),
            i);
    }

    for (const RuntimePerfFunctionTiming& timing : latest_snapshot.functions)
    {
        if (!runtime_timing_has_activity(timing))
            continue;
        const std::string key = runtime_perf_function_key(
            timing.source_file_path,
            timing.owner_qualified_name,
            timing.function_name);
        const auto existing = indices_by_key.find(key);
        if (existing == indices_by_key.end())
        {
            coverage_snapshot.functions.push_back(timing);
            indices_by_key.emplace(key, coverage_snapshot.functions.size() - 1);
        }
        else
        {
            coverage_snapshot.functions[existing->second] = timing;
        }
    }
}
}

std::chrono::milliseconds MetricsOverlayController::refresh_interval()
{
    return kLivePerfRefreshTick;
}

bool is_live_perf_overlay(OverlayMode mode)
{
    return mode == OverlayMode::Perf || mode == OverlayMode::Coverage;
}

bool is_overlay_active(OverlayMode mode)
{
    return mode != OverlayMode::None;
}

void MetricsOverlayController::reset()
{
    metrics_.reset();
    lcov_lookup_.reset();
    clear_coverage_snapshot(coverage_snapshot_);
    last_live_generation_ = 0;
    last_refresh_ = std::chrono::steady_clock::now();
}

void MetricsOverlayController::set_collection_enabled(bool biology_view, OverlayMode mode)
{
    runtime_perf_collector().set_enabled(!biology_view && is_live_perf_overlay(mode));
}

void MetricsOverlayController::adopt_build_metrics(std::shared_ptr<const LiveCityMetricsSnapshot> metrics)
{
    metrics_ = std::move(metrics);
    last_live_generation_ = 0;
}

bool MetricsOverlayController::refresh_live_metrics(
    std::chrono::steady_clock::time_point now,
    OverlayMode mode,
    const SemanticMegacityModel* model)
{
    if (!is_live_perf_overlay(mode) || !model || now - last_refresh_ < kLivePerfRefreshTick)
        return false;

    last_refresh_ = now;
    const RuntimePerfSnapshot snapshot = runtime_perf_collector().latest_snapshot();
    if (snapshot.generation == last_live_generation_)
        return false;

    const RuntimePerfSnapshot* source = &snapshot;
    if (mode == OverlayMode::Coverage)
    {
        merge_runtime_perf_coverage(coverage_snapshot_, snapshot);
        source = &coverage_snapshot_;
    }
    metrics_ = std::make_shared<LiveCityMetricsSnapshot>(
        build_live_city_metrics_snapshot(*model, source, mode == OverlayMode::Coverage));
    last_live_generation_ = snapshot.generation;
    return true;
}

std::shared_ptr<const LiveCityMetricsSnapshot> MetricsOverlayController::metrics() const
{
    return metrics_;
}

std::shared_ptr<const LiveCityPerfDebugState> MetricsOverlayController::build_debug_state(
    OverlayMode mode,
    const SemanticMegacityModel* model) const
{
    if (!model)
        return nullptr;
    if (mode == OverlayMode::LcovCoverage && lcov_lookup_)
        return std::make_shared<LiveCityPerfDebugState>(build_lcov_city_perf_debug_state(*model, *lcov_lookup_));

    const RuntimePerfSnapshot snapshot = runtime_perf_collector().latest_snapshot();
    const RuntimePerfSnapshot* source = mode == OverlayMode::Coverage ? &coverage_snapshot_ : &snapshot;
    return std::make_shared<LiveCityPerfDebugState>(
        build_live_city_perf_debug_state(*model, source, mode == OverlayMode::Coverage));
}

bool MetricsOverlayController::apply_mode_transition(
    OverlayMode previous,
    OverlayMode current,
    const SemanticMegacityModel* model)
{
    if ((previous == OverlayMode::Coverage) != (current == OverlayMode::Coverage))
    {
        clear_coverage_snapshot(coverage_snapshot_);
        last_live_generation_ = 0;
    }

    const bool lcov_toggled = (previous == OverlayMode::LcovCoverage) != (current == OverlayMode::LcovCoverage);
    if (!lcov_toggled)
        return false;
    if (current != OverlayMode::LcovCoverage)
    {
        lcov_lookup_.reset();
        return false;
    }

    if (!load_lcov_lookup(true) || !model)
        return false;
    metrics_ = std::make_shared<LiveCityMetricsSnapshot>(
        build_lcov_city_metrics_snapshot(*model, *lcov_lookup_));
    return true;
}

void MetricsOverlayController::rebuild_lcov_metrics(OverlayMode mode, const SemanticMegacityModel* model)
{
    if (mode != OverlayMode::LcovCoverage || !model)
        return;
    if (!lcov_lookup_)
        load_lcov_lookup(false);
    if (lcov_lookup_)
        metrics_ = std::make_shared<LiveCityMetricsSnapshot>(
            build_lcov_city_metrics_snapshot(*model, *lcov_lookup_));
}

bool MetricsOverlayController::load_lcov_lookup(bool prefer_newest_report)
{
    const std::filesystem::path repo_root(DRAXUL_REPO_ROOT);
    const std::filesystem::path db_lcov = repo_root / "db" / "coverage.lcov";
    const std::filesystem::path build_lcov = repo_root / "build" / "coverage.lcov";
    std::filesystem::path lcov_path = build_lcov;
    if (prefer_newest_report)
    {
        std::error_code ec;
        const auto db_time = std::filesystem::last_write_time(db_lcov, ec);
        const bool db_ok = !ec;
        ec.clear();
        const auto build_time = std::filesystem::last_write_time(build_lcov, ec);
        const bool build_ok = !ec;
        if (db_ok && build_ok)
            lcov_path = build_time >= db_time ? build_lcov : db_lcov;
        else if (db_ok)
            lcov_path = db_lcov;
        else if (!build_ok)
            lcov_path.clear();
    }

    const LcovCoverageReport report = lcov_path.empty() ? LcovCoverageReport{} : load_lcov_file(lcov_path);
    if (report.total_functions == 0)
    {
        lcov_lookup_.reset();
        DRAXUL_LOG_WARN(LogCategory::App, "No LCOV file found (checked db/ and build/)");
        return false;
    }

    lcov_lookup_ = std::make_shared<LcovFunctionLookup>(build_lcov_lookup(report, repo_root));
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "LCOV loaded from %s: %u total functions, %u covered",
        lcov_path.string().c_str(), report.total_functions, report.covered_functions);
    return true;
}

} // namespace draxul
