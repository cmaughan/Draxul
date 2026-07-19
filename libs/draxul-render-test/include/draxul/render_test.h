#pragma once

#include <cstddef>
#include <cstdint>
#include <draxul/app_options.h>
#include <draxul/renderer.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

struct RenderTestScenario
{
    std::string name;
    std::filesystem::path scenario_path;
    std::filesystem::path font_path;
    std::vector<std::string> fallback_paths;
    HostKind host_kind = HostKind::Nvim;
    std::string host_command;
    std::vector<std::string> host_args;
    std::vector<std::string> commands;
    int width = 1280;
    int height = 800;
    float font_size = kDefaultFontPointSize;
    int timeout_ms = 5000;
    int settle_ms = 100;
    int pixel_tolerance = 8;
    double changed_pixels_threshold_pct = 0.1;
    float display_ppi = 0.0f;
    bool debug_overlay = false;
    bool enable_ligatures = true;

    std::filesystem::path reference_image_path() const;
    std::filesystem::path actual_image_path() const;
    std::filesystem::path diff_image_path() const;
    std::filesystem::path report_path() const;
    AppOptions make_app_options() const;
};

std::optional<RenderTestScenario> load_render_test_scenario(const std::filesystem::path& path, std::string* error_message = nullptr);
bool export_render_test_frame(const std::filesystem::path& path, const CapturedFrame& frame, std::string* error_message = nullptr);
bool finalize_render_test_result(const RenderTestScenario& scenario, const CapturedFrame& frame, bool bless_reference, std::string* error_message = nullptr);
void write_render_test_failure_report(const RenderTestScenario& scenario, std::string_view error_message);

// Pure frame comparison used by the render-test finalizer. Exposed for unit tests
// (no GPU/window/Neovim needed). `changed_pixels` counts pixels whose maximum
// per-channel absolute delta is strictly greater than `tolerance`; `passed` is
// `changed_pixels_pct <= threshold_pct`. An invalid frame or a dimension mismatch
// performs no comparison and returns `passed == false`.
struct RenderDiff
{
    bool passed = false;
    std::size_t changed_pixels = 0;
    double changed_pixels_pct = 0.0;
    double mean_abs_channel_diff = 0.0;
    std::uint8_t max_channel_diff = 0;
    CapturedFrame diff_image;
};

RenderDiff compare_frames(const CapturedFrame& actual, const CapturedFrame& reference, int tolerance, double threshold_pct);

} // namespace draxul
