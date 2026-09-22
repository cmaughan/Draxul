#include <draxul/json_util.h>
#include <draxul/perf_timing.h>
#include <draxul/render_test.h>
#include <draxul/text_service.h>
#include <draxul/toml_support.h>

#include <draxul/bmp.h>
#include <draxul/log.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace draxul
{

namespace
{

std::string platform_suffix()
{
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::string normalized_path_string(const std::filesystem::path& path)
{
    return path.lexically_normal().generic_string();
}

std::string expand_placeholders(std::string value, const std::filesystem::path& scenario_dir)
{
    PERF_MEASURE();
    const std::array replacements = {
        std::pair<std::string_view, std::string>{ "${SCENARIO_DIR}", normalized_path_string(scenario_dir) },
        std::pair<std::string_view, std::string>{ "${PROJECT_ROOT}", normalized_path_string(std::filesystem::path{ DRAXUL_PROJECT_ROOT }) },
        std::pair<std::string_view, std::string>{ "${BUILD_ROOT}", normalized_path_string(std::filesystem::path{ DRAXUL_BUILD_ROOT }) },
    };

    for (const auto& [needle, replacement] : replacements)
    {
        size_t pos = 0;
        while ((pos = value.find(needle, pos)) != std::string::npos)
        {
            value.replace(pos, needle.size(), replacement);
            pos += replacement.size();
        }
    }
    return value;
}

bool atomic_replace_file(const std::filesystem::path& temporary,
    const std::filesystem::path& destination, std::string& error)
{
#ifdef _WIN32
    DWORD last_error = ERROR_SUCCESS;
    for (DWORD attempt = 0; attempt < 20; ++attempt)
    {
        if (MoveFileExW(temporary.c_str(), destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        last_error = GetLastError();
        if (last_error != ERROR_ACCESS_DENIED
            && last_error != ERROR_SHARING_VIOLATION)
            break;
        Sleep(std::min<DWORD>(25 * (attempt + 1), 100));
    }
    error = "Unable to atomically replace plugin publication marker (error "
        + std::to_string(last_error) + ")";
    return false;
#else
    if (::rename(temporary.c_str(), destination.c_str()) == 0)
        return true;
    error = "Unable to atomically replace plugin publication marker";
    return false;
#endif
}

bool valid_generation_name(std::string_view value)
{
    return !value.empty() && value != "." && value != ".."
        && value.find('/') == std::string_view::npos
        && value.find('\\') == std::string_view::npos;
}

} // namespace

RenderDiff compare_frames(const CapturedFrame& actual, const CapturedFrame& reference, int tolerance, double threshold_pct)
{
    PERF_MEASURE();
    RenderDiff diff;
    diff.diff_image.width = actual.width;
    diff.diff_image.height = actual.height;
    diff.diff_image.rgba.resize(actual.rgba.size(), 0);

    if (!actual.valid() || !reference.valid() || actual.width != reference.width || actual.height != reference.height)
        return diff;

    uint64_t diff_sum = 0;
    for (size_t i = 0; i < actual.rgba.size(); i += 4)
    {
        const auto dr = static_cast<uint8_t>(std::abs(int(actual.rgba[i + 0]) - int(reference.rgba[i + 0])));
        const auto dg = static_cast<uint8_t>(std::abs(int(actual.rgba[i + 1]) - int(reference.rgba[i + 1])));
        const auto db = static_cast<uint8_t>(std::abs(int(actual.rgba[i + 2]) - int(reference.rgba[i + 2])));
        const auto da = static_cast<uint8_t>(std::abs(int(actual.rgba[i + 3]) - int(reference.rgba[i + 3])));
        const uint8_t max_delta = std::max({ dr, dg, db, da });

        if (max_delta > tolerance)
            ++diff.changed_pixels;

        diff.max_channel_diff = std::max(diff.max_channel_diff, max_delta);
        diff_sum += static_cast<uint64_t>(dr) + dg + db + da;

        diff.diff_image.rgba[i + 0] = dr;
        diff.diff_image.rgba[i + 1] = dg;
        diff.diff_image.rgba[i + 2] = db;
        diff.diff_image.rgba[i + 3] = 255;
    }

    const double total_pixels = static_cast<double>(actual.width) * actual.height;
    diff.changed_pixels_pct = total_pixels > 0.0 ? (static_cast<double>(diff.changed_pixels) * 100.0) / total_pixels : 0.0;
    diff.mean_abs_channel_diff = actual.rgba.empty() ? 0.0 : static_cast<double>(diff_sum) / static_cast<double>(actual.rgba.size());
    diff.passed = diff.changed_pixels_pct <= threshold_pct;
    return diff;
}

namespace
{

bool write_report(const std::filesystem::path& path, const RenderTestScenario& scenario, const CapturedFrame& actual,
    const std::optional<CapturedFrame>& reference, const RenderDiff* diff, bool blessed)
{
    PERF_MEASURE();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    if (!out)
        return false;

    out << "{\n";
    out << "  \"name\": \"" << json_escape_string(scenario.name) << "\",\n";
    out << "  \"platform\": \"" << platform_suffix() << "\",\n";
    out << "  \"width\": " << actual.width << ",\n";
    out << "  \"height\": " << actual.height << ",\n";
    out << "  \"pixel_tolerance\": " << scenario.pixel_tolerance << ",\n";
    out << "  \"changed_pixels_threshold_pct\": " << scenario.changed_pixels_threshold_pct << ",\n";
    out << "  \"blessed\": " << (blessed ? "true" : "false");
    if (reference && diff)
    {
        out << ",\n";
        out << "  \"changed_pixels\": " << diff->changed_pixels << ",\n";
        out << "  \"changed_pixels_pct\": " << diff->changed_pixels_pct << ",\n";
        out << "  \"mean_abs_channel_diff\": " << diff->mean_abs_channel_diff << ",\n";
        out << "  \"max_channel_diff\": " << static_cast<int>(diff->max_channel_diff) << ",\n";
        out << "  \"passed\": " << (diff->passed ? "true" : "false") << '\n';
    }
    else
    {
        out << '\n';
    }
    out << "}\n";
    return out.good();
}

void write_failure_report(const std::filesystem::path& path, const RenderTestScenario& scenario, std::string_view error_message)
{
    PERF_MEASURE();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    if (!out)
        return;

    out << "{\n";
    out << "  \"name\": \"" << json_escape_string(scenario.name) << "\",\n";
    out << "  \"platform\": \"" << platform_suffix() << "\",\n";
    out << "  \"error\": \"" << json_escape_string(error_message) << "\"\n";
    out << "}\n";

    DRAXUL_LOG_ERROR(draxul::LogCategory::Test, "[%s] %.*s", scenario.name.c_str(),
        static_cast<int>(error_message.size()), error_message.data());
}

std::filesystem::path default_output_path(const std::filesystem::path& scenario_path, std::string_view stem_suffix, std::string_view extension)
{
    PERF_MEASURE();
    const auto scenario_dir = scenario_path.parent_path();
    const auto stem = scenario_path.stem().string();
    return scenario_dir / "out" / (stem + "." + platform_suffix() + std::string(stem_suffix) + std::string(extension));
}

} // namespace

std::filesystem::path RenderTestScenario::reference_image_path() const
{
    return scenario_path.parent_path() / "reference" / (scenario_path.stem().string() + "." + platform_suffix() + ".bmp");
}

std::filesystem::path RenderTestScenario::actual_image_path() const
{
    return default_output_path(scenario_path, ".actual", ".bmp");
}

std::filesystem::path RenderTestScenario::diff_image_path() const
{
    return default_output_path(scenario_path, ".diff", ".bmp");
}

std::filesystem::path RenderTestScenario::report_path() const
{
    return default_output_path(scenario_path, ".report", ".json");
}

AppOptions RenderTestScenario::make_app_options() const
{
    PERF_MEASURE();
    AppOptions options;
    options.load_user_config = false;
    options.save_user_config = false;
    options.activate_window_on_startup = false;
    options.show_diagnostics_in_render_test = debug_overlay;
    options.show_system_resources = host_kind != HostKind::Plugin;
    options.clamp_window_to_display = false;
    options.render_target_pixel_width = width;
    options.render_target_pixel_height = height;
    options.config_overrides.window_width = width;
    options.config_overrides.window_height = height;
    options.config_overrides.font_size = font_size;
    options.config_overrides.enable_ligatures = enable_ligatures;
    if (display_ppi > 0.0f)
        options.override_display_ppi = display_ppi;
    if (!font_path.empty())
        options.config_overrides.font_path = normalized_path_string(font_path);
    options.config_overrides.fallback_paths = fallback_paths;
    options.host_kind = host_kind;
    options.host_command = host_command;
    options.host_args = host_args;
    options.host_source_path = normalized_path_string(source_path);
    options.host_plugin_id = plugin_id;
    options.host_plugin_config_json = plugin_config_json;
    options.startup_commands = commands;
    return options;
}

std::optional<RenderTestScenario> load_render_test_scenario(const std::filesystem::path& path, std::string* error_message)
{
    PERF_MEASURE();
    std::string parse_error;
    auto document = toml_support::parse_file(path, &parse_error);
    if (!document)
    {
        if (error_message)
            *error_message = parse_error.empty() ? "Unable to open render test scenario" : parse_error;
        return std::nullopt;
    }

    RenderTestScenario scenario;
    scenario.scenario_path = std::filesystem::absolute(path).lexically_normal();
    scenario.name = scenario.scenario_path.stem().string();
    const auto scenario_dir = scenario.scenario_path.parent_path();

    if (auto name = toml_support::get_string(*document, "name"); name.has_value())
        scenario.name = *name;
    if (auto width = toml_support::get_int(*document, "width"); width.has_value())
        scenario.width = static_cast<int>(*width);
    if (auto height = toml_support::get_int(*document, "height"); height.has_value())
        scenario.height = static_cast<int>(*height);
    if (auto font_size = toml_support::get_double(*document, "font_size"); font_size.has_value())
        scenario.font_size = static_cast<float>(*font_size);
    else if (auto font_size_int = toml_support::get_int(*document, "font_size"); font_size_int.has_value())
        scenario.font_size = static_cast<float>(*font_size_int);
    if (auto timeout_ms = toml_support::get_int(*document, "timeout_ms"); timeout_ms.has_value())
        scenario.timeout_ms = static_cast<int>(*timeout_ms);
    if (auto settle_ms = toml_support::get_int(*document, "settle_ms"); settle_ms.has_value())
        scenario.settle_ms = static_cast<int>(*settle_ms);
    if (auto pixel_tolerance = toml_support::get_int(*document, "pixel_tolerance"); pixel_tolerance.has_value())
        scenario.pixel_tolerance = static_cast<int>(*pixel_tolerance);
    if (auto changed_pixels_threshold_pct = toml_support::get_double(*document, "changed_pixels_threshold_pct"); changed_pixels_threshold_pct.has_value())
        scenario.changed_pixels_threshold_pct = *changed_pixels_threshold_pct;
    if (auto display_ppi = toml_support::get_double(*document, "display_ppi"); display_ppi.has_value())
        scenario.display_ppi = static_cast<float>(*display_ppi);
    if (auto debug_overlay = toml_support::get_bool(*document, "debug_overlay"); debug_overlay.has_value())
        scenario.debug_overlay = *debug_overlay;
    if (auto enable_ligatures = toml_support::get_bool(*document, "enable_ligatures"); enable_ligatures.has_value())
        scenario.enable_ligatures = *enable_ligatures;
    if (auto font_path = toml_support::get_string(*document, "font_path"))
        scenario.font_path = std::filesystem::path(expand_placeholders(*font_path, scenario_dir));
    if (auto fallback_paths = toml_support::get_string_array(*document, "fallback_paths"))
    {
        scenario.fallback_paths.clear();
        for (auto& entry : *fallback_paths)
            scenario.fallback_paths.push_back(expand_placeholders(entry, scenario_dir));
    }
    if (auto host = toml_support::get_string(*document, "host"))
    {
        if (auto parsed = parse_host_kind(*host))
            scenario.host_kind = *parsed;
    }
    if (auto host_command = toml_support::get_string(*document, "host_command"))
        scenario.host_command = expand_placeholders(*host_command, scenario_dir);
    if (auto host_args = toml_support::get_string_array(*document, "host_args"))
    {
        scenario.host_args.clear();
        for (auto& entry : *host_args)
            scenario.host_args.push_back(expand_placeholders(entry, scenario_dir));
    }
    if (auto source = toml_support::get_string(*document, "source"))
        scenario.source_path = std::filesystem::path(
            expand_placeholders(*source, scenario_dir));
    if (auto plugin_id = toml_support::get_string(*document, "plugin_id"))
        scenario.plugin_id = *plugin_id;
    if (auto plugin_config = toml_support::get_string(
            *document, "plugin_config_json"))
        scenario.plugin_config_json
            = expand_placeholders(*plugin_config, scenario_dir);
    if (auto reload_plugin_id = toml_support::get_string(
            *document, "reload_plugin_id"))
        scenario.reload_plugin_id = *reload_plugin_id;
    if (auto reload_plugin_package = toml_support::get_string(
            *document, "reload_plugin_package"))
        scenario.reload_plugin_package = std::filesystem::path(
            expand_placeholders(*reload_plugin_package, scenario_dir));
    if (auto nvim_args = toml_support::get_string_array(*document, "nvim_args"))
    {
        scenario.host_args.clear();
        for (auto& entry : *nvim_args)
            scenario.host_args.push_back(expand_placeholders(entry, scenario_dir));
    }
    if (auto commands = toml_support::get_string_array(*document, "commands"))
    {
        scenario.commands.clear();
        for (auto& entry : *commands)
            scenario.commands.push_back(expand_placeholders(entry, scenario_dir));
    }

    scenario.width = std::clamp(scenario.width, 320, 3840);
    scenario.height = std::clamp(scenario.height, 240, 2160);
    scenario.font_size = std::clamp(scenario.font_size, TextService::MIN_POINT_SIZE, TextService::MAX_POINT_SIZE);
    scenario.timeout_ms = std::clamp(scenario.timeout_ms, 1000, 30000);
    scenario.settle_ms = std::clamp(scenario.settle_ms, 10, 5000);
    scenario.pixel_tolerance = std::clamp(scenario.pixel_tolerance, 0, 255);
    scenario.changed_pixels_threshold_pct = std::max(0.0, scenario.changed_pixels_threshold_pct);

    if (scenario.commands.empty())
    {
        if (error_message)
            *error_message = "Render test scenario requires at least one startup command";
        return std::nullopt;
    }
    if (scenario.reload_plugin_id.empty()
        != scenario.reload_plugin_package.empty())
    {
        if (error_message)
            *error_message = "Render-test plugin reload requires both reload_plugin_id and reload_plugin_package";
        return std::nullopt;
    }

    return scenario;
}

std::optional<RenderTestPluginPublication>
publish_render_test_plugin_generation(
    const std::filesystem::path& published_source_root,
    const std::filesystem::path& destination_root,
    std::string* error_message)
{
    PERF_MEASURE();
    std::filesystem::path installed_generation;
    std::filesystem::path temporary_marker;
    try
    {
        const auto source_pointer = published_source_root / "current.json";
        std::ifstream pointer_input(source_pointer, std::ios::binary);
        if (!pointer_input)
            throw std::runtime_error(
                "Replacement plugin package has no current.json");
        const auto pointer = nlohmann::json::parse(pointer_input);
        const std::string generation = pointer.value("generation", "");
        if (!valid_generation_name(generation))
            throw std::runtime_error(
                "Replacement plugin package has an invalid generation");

        const auto source_generation = published_source_root / "generations"
            / generation;
        if (!std::filesystem::is_regular_file(
                source_generation / "plugin.toml"))
            throw std::runtime_error(
                "Replacement plugin generation has no plugin.toml");

        std::filesystem::create_directories(destination_root / "generations");
        static std::atomic<uint64_t> serial{ 0 };
        const auto incoming = destination_root
            / (".render-test-incoming-"
                + std::to_string(serial.fetch_add(1)));
        const auto destination_generation = destination_root / "generations"
            / generation;
        installed_generation = destination_generation;
        std::error_code filesystem_error;
        std::filesystem::remove_all(incoming, filesystem_error);
        if (std::filesystem::exists(destination_generation))
            throw std::runtime_error(
                "Replacement plugin generation is already published: "
                + generation);
        std::filesystem::copy(source_generation, incoming,
            std::filesystem::copy_options::recursive,
            filesystem_error);
        if (filesystem_error)
            throw std::runtime_error(
                "Unable to copy replacement plugin generation: "
                + filesystem_error.message());
        std::filesystem::rename(incoming, destination_generation,
            filesystem_error);
        if (filesystem_error)
        {
            std::filesystem::remove_all(incoming);
            throw std::runtime_error(
                "Unable to publish replacement plugin generation: "
                + filesystem_error.message());
        }

        RenderTestPluginPublication publication;
        publication.destination_root = destination_root;
        publication.published_generation = destination_generation;
        const auto destination_pointer = destination_root / "current.json";
        if (std::filesystem::is_regular_file(destination_pointer))
        {
            std::ifstream previous(destination_pointer, std::ios::binary);
            if (!previous)
                throw std::runtime_error(
                    "Unable to read the current plugin publication marker");
            publication.previous_pointer.assign(
                std::istreambuf_iterator<char>(previous), {});
            publication.had_previous_pointer = true;
        }

        const auto temporary = destination_root
            / ("current.json.render-test-"
                + std::to_string(serial.fetch_add(1)));
        temporary_marker = temporary;
        std::filesystem::copy_file(source_pointer, temporary,
            std::filesystem::copy_options::overwrite_existing);
        std::string replace_error;
        if (!atomic_replace_file(temporary, destination_pointer,
                replace_error))
        {
            std::filesystem::remove(temporary);
            std::filesystem::remove_all(destination_generation);
            throw std::runtime_error(replace_error);
        }
        return publication;
    }
    catch (const std::exception& exception)
    {
        std::error_code cleanup_error;
        if (!temporary_marker.empty())
            std::filesystem::remove(temporary_marker, cleanup_error);
        if (!installed_generation.empty())
            std::filesystem::remove_all(
                installed_generation, cleanup_error);
        if (error_message)
            *error_message = exception.what();
        return std::nullopt;
    }
}

bool restore_render_test_plugin_generation(
    const RenderTestPluginPublication& publication,
    std::string* error_message)
{
    PERF_MEASURE();
    try
    {
        const auto pointer = publication.destination_root / "current.json";
        if (publication.had_previous_pointer)
        {
            const auto temporary = publication.destination_root
                / "current.json.render-test-restore";
            {
                std::ofstream output(temporary,
                    std::ios::binary | std::ios::trunc);
                output << publication.previous_pointer;
                if (!output)
                    throw std::runtime_error(
                        "Unable to write restored plugin publication marker");
            }
            std::string replace_error;
            if (!atomic_replace_file(temporary, pointer, replace_error))
                throw std::runtime_error(replace_error);
        }
        else
        {
            std::filesystem::remove(pointer);
        }
        std::error_code cleanup_error;
        std::filesystem::remove_all(
            publication.published_generation, cleanup_error);
        if (cleanup_error)
            throw std::runtime_error(
                "Unable to remove render-test plugin generation: "
                + cleanup_error.message());
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error_message)
            *error_message = exception.what();
        return false;
    }
}

bool finalize_render_test_result(const RenderTestScenario& scenario, const CapturedFrame& frame, bool bless_reference, std::string* error_message)
{
    PERF_MEASURE();
    if (!frame.valid())
    {
        if (error_message)
            *error_message = "Captured frame is empty";
        return false;
    }

    if (!write_bmp_rgba(scenario.actual_image_path(), frame))
    {
        if (error_message)
            *error_message = "Failed to write actual capture image";
        return false;
    }

    if (bless_reference)
    {
        if (!write_bmp_rgba(scenario.reference_image_path(), frame))
        {
            if (error_message)
                *error_message = "Failed to write blessed reference image";
            return false;
        }
        write_report(scenario.report_path(), scenario, frame, std::nullopt, nullptr, true);
        return true;
    }

    auto reference = read_bmp_rgba(scenario.reference_image_path());
    if (!reference)
    {
        if (error_message)
            *error_message = "Reference image not found; rerun with --bless-render-test";
        return false;
    }

    if (reference->width != frame.width || reference->height != frame.height)
    {
        if (error_message)
        {
            std::ostringstream oss;
            oss << "Reference image size (" << reference->width << "x" << reference->height
                << ") does not match the captured frame (" << frame.width << "x" << frame.height << ")";
            *error_message = oss.str();
        }
        return false;
    }

    RenderDiff diff = compare_frames(frame, *reference, scenario.pixel_tolerance, scenario.changed_pixels_threshold_pct);
    if (!write_bmp_rgba(scenario.diff_image_path(), diff.diff_image))
    {
        if (error_message)
            *error_message = "Failed to write diff image";
        return false;
    }

    write_report(scenario.report_path(), scenario, frame, reference, &diff, false);

    if (!diff.passed && error_message)
    {
        std::ostringstream message;
        message << "Render snapshot drifted by " << diff.changed_pixels_pct << "% (" << diff.changed_pixels
                << " pixels, tolerance " << scenario.changed_pixels_threshold_pct << "%)";
        *error_message = message.str();
    }
    return diff.passed;
}

bool export_render_test_frame(const std::filesystem::path& path, const CapturedFrame& frame, std::string* error_message)
{
    PERF_MEASURE();
    if (!frame.valid())
    {
        if (error_message)
            *error_message = "Captured frame is empty";
        return false;
    }

    if (!write_bmp_rgba(path, frame))
    {
        if (error_message)
            *error_message = "Failed to write exported capture image";
        return false;
    }

    return true;
}

void write_render_test_failure_report(const RenderTestScenario& scenario, std::string_view error_message)
{
    PERF_MEASURE();
    write_failure_report(scenario.report_path(), scenario, error_message);
}

} // namespace draxul
