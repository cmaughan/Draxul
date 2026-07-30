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
    bool help = false;
    bool want_console = false;
    bool smoke_test = false;
    bool continuous_refresh = false;
    bool no_vblank = false;
    bool no_ui = false;
    bool list_sessions = false;
    bool new_session = false;
    bool rename_session = false;
    bool delete_session = false;
    bool delete_all_sessions = false;
    bool server = false;
    bool server_status = false;
    bool shutdown_server = false;
    bool force_stop_server = false;
    bool confirmed = false;
    bool experimental_server_client = false;
    bool experimental_remote_terminal = false;
    bool experimental_remote_shell = false;
    bool no_server = false;
    bool json_output = false;
    std::filesystem::path server_runtime_dir;
    std::string server_shell_kind;
    std::string server_command;
    std::filesystem::path server_working_dir;
    int server_scrollback_lines = 10000;
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
bool should_use_shared_server(const ParsedArgs& args);
bool should_bootstrap_experimental_server(const ParsedArgs& args);
std::optional<std::string> validate_host_provider_availability(
    const ParsedArgs& args, const HostProviderRegistry& registry);

} // namespace draxul
