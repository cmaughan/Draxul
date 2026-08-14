#pragma once

#include "lcov_coverage.h"
#include "live_city_metrics.h"

#include <chrono>
#include <draxul/megacity_code_config.h>
#include <draxul/perf_timing.h>
#include <filesystem>
#include <memory>

namespace draxul
{

struct SemanticMegacityModel;

bool is_live_perf_overlay(OverlayMode mode);
bool is_overlay_active(OverlayMode mode);

// Owns the mutable runtime/coverage accumulation used by the host overlays.
// All methods are called on the main thread; published metric snapshots remain
// immutable shared values consumed by scene construction.
class MetricsOverlayController
{
public:
    static std::chrono::milliseconds refresh_interval();

    void reset();
    void set_source_root(std::filesystem::path source_root);
    void set_collection_enabled(bool biology_view, OverlayMode mode);

    void adopt_build_metrics(std::shared_ptr<const LiveCityMetricsSnapshot> metrics);
    bool refresh_live_metrics(
        std::chrono::steady_clock::time_point now,
        OverlayMode mode,
        const SemanticMegacityModel* model);

    std::shared_ptr<const LiveCityMetricsSnapshot> metrics() const;
    std::shared_ptr<const LiveCityPerfDebugState> build_debug_state(
        OverlayMode mode,
        const SemanticMegacityModel* model) const;

    bool apply_mode_transition(
        OverlayMode previous,
        OverlayMode current,
        const SemanticMegacityModel* model);
    void rebuild_lcov_metrics(OverlayMode mode, const SemanticMegacityModel* model);

private:
    bool load_lcov_lookup(bool prefer_newest_report);

    std::shared_ptr<const LiveCityMetricsSnapshot> metrics_;
    std::filesystem::path source_root_;
    std::shared_ptr<const LcovFunctionLookup> lcov_lookup_;
    RuntimePerfSnapshot coverage_snapshot_;
    uint64_t last_live_generation_ = 0;
    std::chrono::steady_clock::time_point last_refresh_ = std::chrono::steady_clock::now();
};

} // namespace draxul
