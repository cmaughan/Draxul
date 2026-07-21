#pragma once

#include <draxul/host_kind.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

class HostProviderRegistry;

struct ParsedArgs
{
    bool want_console = false;
    bool smoke_test = false;
    bool continuous_refresh = false;
    bool no_vblank = false;
    bool no_ui = false;
    bool list_sessions = false;
    bool new_session = false;
    bool rename_session = false;
    bool delete_session = false;
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    bool bless_render_test = false;
    bool show_render_test_window = false;
    std::filesystem::path render_test_path;
    std::filesystem::path export_render_test_path;
#endif
    std::optional<HostKind> host_kind;
    std::string host_command;
    std::filesystem::path host_source_path;
    std::string session_id = "default";
    bool session_id_explicit = false;
    std::string session_name;
    std::string log_file;
    std::string log_level;
    std::string pty_capture_file;
    std::filesystem::path screenshot_path;
    int screenshot_delay_ms = 6000;
    int screenshot_width = 0;
    int screenshot_height = 0;
    // Test hook: dispatch a GUI action (see <draxul/gui_actions.h>) once
    // content is ready, before the screenshot capture.
    std::string gui_action;
};

struct ParseArgsResult
{
    ParsedArgs args;
    // When set, parsing failed; the message is the error to display to the user.
    // The caller is responsible for printing it and exiting non-zero.
    std::optional<std::string> error;
};

// Parses CLI arguments. Numeric / format errors are returned via
// `result.error` rather than calling std::exit, so the parser is testable
// without spawning a subprocess. The first element of `args` (program name)
// is ignored, mirroring argv[0].
ParseArgsResult parse_args(const std::vector<std::string>& args);
std::optional<std::string> validate_host_provider_availability(
    const ParsedArgs& args, const HostProviderRegistry& registry);

} // namespace draxul
