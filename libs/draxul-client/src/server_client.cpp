#include <draxul/server_client.h>

#include <draxul/control_plane.h>
#include <draxul/process_util.h>

#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#include <unistd.h>
#endif

namespace draxul
{

namespace
{

constexpr std::string_view kStartingMarkerPrefix = "server-starting-";
constexpr auto kStartingMarkerLifetime = std::chrono::seconds(5);
constexpr auto kEndpointPublicationGrace = std::chrono::seconds(2);

std::optional<nlohmann::json> read_bounded_json(
    const std::filesystem::path& path)
{
    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error || size == 0 || size > 16 * 1024)
        return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    auto value = nlohmann::json::parse(bytes, nullptr, false);
    if (value.is_discarded() || !value.is_object())
        return std::nullopt;
    return value;
}

#ifdef _WIN32
bool prepare_windows_server_helper(
    const std::filesystem::path& client_executable,
    std::filesystem::path& server_executable,
    std::string& error)
{
    server_executable
        = windows_server_helper_executable(client_executable);
    if (server_executable == client_executable)
        return true;

    static std::mutex helper_refresh_mutex;
    const std::lock_guard lock(helper_refresh_mutex);
    std::error_code source_size_error;
    std::error_code helper_size_error;
    std::error_code source_time_error;
    std::error_code helper_time_error;
    const auto source_size = std::filesystem::file_size(
        client_executable, source_size_error);
    const auto helper_size = std::filesystem::file_size(
        server_executable, helper_size_error);
    const auto source_time = std::filesystem::last_write_time(
        client_executable, source_time_error);
    const auto helper_time = std::filesystem::last_write_time(
        server_executable, helper_time_error);
    if (!source_size_error && !helper_size_error
        && !source_time_error && !helper_time_error
        && source_size == helper_size
        && source_time == helper_time)
    {
        return true;
    }

    const std::wstring temporary_name
        = server_executable.wstring() + L".tmp-"
        + std::to_wstring(GetCurrentProcessId()) + L"-"
        + std::to_wstring(GetCurrentThreadId());
    const std::filesystem::path temporary(temporary_name);
    if (!CopyFileW(client_executable.wstring().c_str(),
            temporary.wstring().c_str(), FALSE))
    {
        error = "Unable to stage the Windows Draxul server helper (error "
            + std::to_string(GetLastError()) + ").";
        return false;
    }
    if (!MoveFileExW(temporary.wstring().c_str(),
            server_executable.wstring().c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD move_error = GetLastError();
        DeleteFileW(temporary.wstring().c_str());
        error = "Unable to refresh the Windows Draxul server helper (error "
            + std::to_string(move_error)
            + "). Stop the existing server and retry.";
        return false;
    }
    if (!source_time_error)
    {
        std::error_code timestamp_error;
        std::filesystem::last_write_time(
            server_executable, source_time, timestamp_error);
    }
    return true;
}
#endif

std::optional<std::string> process_start_token(uint64_t pid)
{
    if (pid == 0)
        return std::nullopt;
#ifdef _WIN32
    if (pid > std::numeric_limits<DWORD>::max())
        return std::nullopt;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
        static_cast<DWORD>(pid));
    if (!process)
        return std::nullopt;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code)
        && code == STILL_ACTIVE;
    // Qualified: the shared HANDLE overload lives at draxul scope and would
    // otherwise be hidden by this anonymous-namespace uint64_t overload.
    const auto token = alive
        ? draxul::process_start_token(process)
        : std::nullopt;
    CloseHandle(process);
    return token;
#elif defined(__APPLE__)
    if (pid > static_cast<uint64_t>(
            std::numeric_limits<pid_t>::max()))
    {
        return std::nullopt;
    }
    kinfo_proc process{};
    size_t size = sizeof(process);
    int query[] = {
        CTL_KERN,
        KERN_PROC,
        KERN_PROC_PID,
        static_cast<int>(pid),
    };
    if (::sysctl(query, 4, &process, &size, nullptr, 0) != 0
        || size == 0)
    {
        return std::nullopt;
    }
    return std::to_string(process.kp_proc.p_starttime.tv_sec)
        + ":" + std::to_string(process.kp_proc.p_starttime.tv_usec);
#else
    if (pid > static_cast<uint64_t>(
            std::numeric_limits<pid_t>::max()))
    {
        return std::nullopt;
    }
    std::ifstream stat(
        "/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(stat, line))
        return std::nullopt;
    const size_t command_end = line.rfind(')');
    if (command_end == std::string::npos
        || command_end + 2 >= line.size())
    {
        return std::nullopt;
    }
    std::istringstream fields(line.substr(command_end + 2));
    std::string field;
    // /proc/<pid>/stat field 22 is the process start time. The substring
    // starts at field 3, so consume through the twentieth value.
    for (int index = 0; index < 20; ++index)
    {
        if (!(fields >> field))
            return std::nullopt;
    }
    std::ifstream boot_id("/proc/sys/kernel/random/boot_id");
    std::string boot;
    if (!std::getline(boot_id, boot) || boot.empty())
        return std::nullopt;
    return boot + ":" + field;
#endif
}

bool process_identity_matches(
    uint64_t pid, std::string_view expected_start_token)
{
    if (expected_start_token.empty())
        return false;
    const auto actual = process_start_token(pid);
    return actual && *actual == expected_start_token;
}

struct RuntimeEvidence
{
    std::string inspection_error;
    bool metadata_exists = false;
    bool metadata_valid = false;
    bool metadata_version_mismatch = false;
    uint64_t metadata_pid = 0;
    std::string metadata_start_token;
    std::string metadata_epoch;
    uint64_t metadata_published_unix_ms = 0;
    bool metadata_process_matches = false;
    bool live_starting_process = false;
    bool stale_starting_marker = false;
    // From server-failed.json (written by ServerKernel when a start dies, and
    // deleted by the next successful start): the actual reason the last
    // launch failed, instead of an unexplained Absent.
    std::string failure_reason;
    uint64_t failure_unix_ms = 0;
};

RuntimeEvidence inspect_runtime(const std::filesystem::path& runtime_directory)
{
    RuntimeEvidence evidence;
    std::error_code runtime_error;
    const auto runtime_status
        = std::filesystem::status(runtime_directory, runtime_error);
    if (runtime_error)
    {
        if (runtime_error
            == std::make_error_code(std::errc::no_such_file_or_directory))
        {
            auto ancestor = runtime_directory.parent_path();
            while (!ancestor.empty())
            {
                std::error_code ancestor_error;
                const auto ancestor_status
                    = std::filesystem::status(ancestor, ancestor_error);
                if (!ancestor_error
                    && std::filesystem::exists(ancestor_status))
                {
                    if (std::filesystem::is_directory(ancestor_status))
                        return evidence;
                    evidence.inspection_error
                        = "Unable to inspect the Draxul server runtime directory: "
                        + std::make_error_code(std::errc::not_a_directory)
                              .message();
                    return evidence;
                }
                if (ancestor_error
                    && ancestor_error
                        != std::make_error_code(
                            std::errc::no_such_file_or_directory))
                {
                    evidence.inspection_error
                        = "Unable to inspect the Draxul server runtime directory: "
                        + ancestor_error.message();
                    return evidence;
                }
                const auto parent = ancestor.parent_path();
                if (parent == ancestor)
                    break;
                ancestor = parent;
            }
            return evidence;
        }
        evidence.inspection_error
            = "Unable to inspect the Draxul server runtime directory: "
            + runtime_error.message();
        return evidence;
    }
    if (!std::filesystem::exists(runtime_status))
        return evidence;

    const auto metadata_path = server_metadata_path(runtime_directory);
    std::error_code metadata_error;
    evidence.metadata_exists
        = std::filesystem::exists(metadata_path, metadata_error);
    if (metadata_error)
    {
        evidence.inspection_error
            = "Unable to inspect the Draxul server runtime directory: "
            + metadata_error.message();
        return evidence;
    }
    if (const auto metadata = read_bounded_json(metadata_path))
    {
        if (metadata->contains("version")
            && (*metadata)["version"].is_number_integer())
        {
            const auto& version = (*metadata)["version"];
            const bool compatible = version.is_number_unsigned()
                ? version.get<uint64_t>()
                    == static_cast<uint64_t>(kControlProtocolVersion)
                : version.get<int64_t>()
                    == static_cast<int64_t>(kControlProtocolVersion);
            evidence.metadata_version_mismatch = !compatible;
        }
        evidence.metadata_valid = metadata->contains("server_pid")
            && (*metadata)["server_pid"].is_number_unsigned()
            && metadata->contains("server_process_start_token")
            && (*metadata)["server_process_start_token"].is_string()
            && !(*metadata)["server_process_start_token"]
                    .get_ref<const std::string&>()
                    .empty();
        if (evidence.metadata_valid)
        {
            evidence.metadata_pid = (*metadata)["server_pid"].get<uint64_t>();
            evidence.metadata_start_token
                = (*metadata)["server_process_start_token"]
                      .get<std::string>();
            evidence.metadata_process_matches
                = process_identity_matches(evidence.metadata_pid,
                    evidence.metadata_start_token);
        }
        if (metadata->contains("server_epoch")
            && (*metadata)["server_epoch"].is_string())
        {
            evidence.metadata_epoch
                = (*metadata)["server_epoch"].get<std::string>();
        }
        if (metadata->contains("published_unix_ms")
            && (*metadata)["published_unix_ms"].is_number_unsigned())
        {
            evidence.metadata_published_unix_ms
                = (*metadata)["published_unix_ms"].get<uint64_t>();
        }
    }

    if (const auto failure
        = read_bounded_json(runtime_directory / "server-failed.json"))
    {
        if (failure->contains("error") && (*failure)["error"].is_string())
            evidence.failure_reason = (*failure)["error"].get<std::string>();
        if (failure->contains("created_unix_ms")
            && (*failure)["created_unix_ms"].is_number_unsigned())
        {
            evidence.failure_unix_ms
                = (*failure)["created_unix_ms"].get<uint64_t>();
        }
    }

    const uint64_t now = current_unix_time_ms();
    std::error_code iteration_error;
    if (!std::filesystem::is_directory(runtime_directory, iteration_error))
    {
        if (iteration_error)
        {
            evidence.inspection_error
                = "Unable to inspect the Draxul server runtime directory: "
                + iteration_error.message();
        }
        return evidence;
    }
    for (std::filesystem::directory_iterator it(runtime_directory, iteration_error);
        !iteration_error && it != std::filesystem::directory_iterator();
        it.increment(iteration_error))
    {
        const std::string name = it->path().filename().string();
        if (!name.starts_with(kStartingMarkerPrefix)
            || it->path().extension() != ".json")
        {
            continue;
        }
        const auto marker = read_bounded_json(it->path());
        const uint64_t marker_pid = marker && marker->contains("pid")
                && (*marker)["pid"].is_number_unsigned()
            ? (*marker)["pid"].get<uint64_t>()
            : 0;
        const std::string marker_start_token
            = marker && marker->contains("process_start_token")
                && (*marker)["process_start_token"].is_string()
            ? (*marker)["process_start_token"].get<std::string>()
            : std::string{};
        const uint64_t created_unix_ms
            = marker && marker->contains("created_unix_ms")
                && (*marker)["created_unix_ms"].is_number_unsigned()
            ? (*marker)["created_unix_ms"].get<uint64_t>()
            : 0;
        const uint64_t lifetime_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<
                std::chrono::milliseconds>(
                kStartingMarkerLifetime)
                .count());
        const bool expired = created_unix_ms == 0
            || (created_unix_ms > now
                && created_unix_ms - now > lifetime_ms)
            || (now >= created_unix_ms
                && now - created_unix_ms > lifetime_ms);
        if (!expired
            && process_identity_matches(
                marker_pid, marker_start_token))
        {
            evidence.live_starting_process = true;
        }
        else
        {
            evidence.stale_starting_marker = true;
            std::error_code remove_error;
            std::filesystem::remove(it->path(), remove_error);
        }
    }
    if (iteration_error)
    {
        evidence.inspection_error
            = "Unable to inspect the Draxul server runtime directory: "
            + iteration_error.message();
    }
    return evidence;
}

ServerProbeResult unavailable_result(
    const RuntimeEvidence& evidence,
    std::string error_code,
    std::string error_message)
{
    if (evidence.metadata_version_mismatch
        || error_code == "unsupported_version")
    {
        return {
            .state = ServerProbeState::Incompatible,
            .error_code = "incompatible_protocol",
            .error_message = evidence.metadata_version_mismatch
                ? "Client/server control protocol versions do not match."
                : std::move(error_message),
        };
    }
    if (evidence.live_starting_process)
    {
        return {
            .state = ServerProbeState::Starting,
            .error_code = std::move(error_code),
            .error_message = std::move(error_message),
        };
    }
    if (evidence.metadata_valid)
    {
        const uint64_t now = current_unix_time_ms();
        const uint64_t grace_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                kEndpointPublicationGrace)
                .count());
        const bool publication_in_grace
            = evidence.metadata_published_unix_ms != 0
            && now >= evidence.metadata_published_unix_ms
            && now - evidence.metadata_published_unix_ms <= grace_ms;
        return {
            .state = evidence.metadata_process_matches
                    && publication_in_grace
                ? ServerProbeState::Busy
                : ServerProbeState::Crashed,
            .error_code = std::move(error_code),
            .error_message = std::move(error_message),
        };
    }
    // A recent failure marker beats the generic "absent"/"stale" story: the
    // server DID launch and recorded why it died. Bounded so an old marker
    // cannot mislabel an unrelated later problem.
    constexpr uint64_t kFailureReasonHorizonMs = 15 * 60 * 1000;
    const uint64_t now_ms = current_unix_time_ms();
    const bool recent_failure = !evidence.failure_reason.empty()
        && evidence.failure_unix_ms != 0 && now_ms >= evidence.failure_unix_ms
        && now_ms - evidence.failure_unix_ms <= kFailureReasonHorizonMs;
    if (recent_failure)
    {
        error_code = "server_start_failed";
        error_message = "The Draxul server exited during startup: "
            + evidence.failure_reason;
    }
    if (evidence.metadata_exists || evidence.stale_starting_marker)
    {
        return {
            .state = ServerProbeState::Stale,
            .error_code = std::move(error_code),
            .error_message = std::move(error_message),
        };
    }
    return {
        .state = ServerProbeState::Absent,
        .error_code = std::move(error_code),
        .error_message = std::move(error_message),
    };
}

} // namespace

std::filesystem::path server_runtime_directory(
    const std::filesystem::path& config_directory)
{
    return config_directory / "runtime" / "server-v1";
}

std::filesystem::path server_metadata_path(
    const std::filesystem::path& runtime_directory)
{
    return control_metadata_path(runtime_directory,
        namespaced_control_id(kServerControlId, runtime_directory));
}

#ifdef _WIN32
std::filesystem::path windows_server_helper_executable(
    const std::filesystem::path& client_executable)
{
    if (_wcsicmp(client_executable.filename().c_str(),
            L"draxul-server.exe")
        == 0)
    {
        return client_executable;
    }
    return client_executable.parent_path()
        / "draxul-server.exe";
}

std::filesystem::path windows_client_executable(
    const std::filesystem::path& current_executable)
{
    if (_wcsicmp(current_executable.filename().c_str(),
            L"draxul-server.exe")
        != 0)
    {
        return current_executable;
    }
    return current_executable.parent_path() / "draxul.exe";
}
#endif

std::string make_server_client_id()
{
    std::random_device random;
    std::ostringstream out;
    out << "ui-" << std::hex << std::setfill('0');
    for (int index = 0; index < 12; ++index)
        out << std::setw(2) << (random() & 0xff);
    return out.str();
}

ServerProbeResult ServerClient::probe(const ServerEnsureOptions& options)
{
    const RuntimeEvidence evidence = inspect_runtime(options.runtime_directory);
    if (!evidence.inspection_error.empty())
    {
        return {
            .state = ServerProbeState::LaunchFailed,
            .error_code = "runtime_unavailable",
            .error_message = evidence.inspection_error,
        };
    }
    if (!evidence.metadata_exists)
        return unavailable_result(evidence, "endpoint_unavailable",
            "No Draxul server endpoint is published.");

    ServerHello hello{
        .protocol_major = options.protocol_major,
        .protocol_minor = options.protocol_minor,
        .client_id = options.client_id.empty()
            ? make_server_client_id()
            : options.client_id,
        .connection_token = options.connection_token,
        .registration_nonce = options.registration_nonce.empty()
            ? make_server_client_id()
            : options.registration_nonce,
        .capabilities = {
            "agent-control-v1",
            "agent-projection-v1",
            std::string(kServerClientTokenCapability),
            "client-registration",
            "controller-lease",
            "fake-remote-terminal",
            "graceful-shutdown",
            "managed-agent-v1",
            "managed-agent-v2",
            "multi-terminal-v1",
            "named-sessions-v1",
            "ordered-terminal-events",
            "real-remote-terminal",
            "session-delete-v1",
            "status",
            "terminal-metrics-v1",
            "terminal-presentation-suspend-v1",
            "terminal-scrollback-v1",
            "terminal-uncompressed-v1",
            "topology-v1",
            "topology-control-v2",
            "client-plugin-pane-v1",
        },
    };
    const auto response = ControlClient::request(
        namespaced_control_id(kServerControlId, options.runtime_directory),
        options.runtime_directory,
        "server.hello", server_hello_to_json(hello),
        { .timeout = options.request_timeout });
    if (!response.ok)
    {
        if (response.error_code == "incompatible_protocol")
        {
            return {
                .state = ServerProbeState::Incompatible,
                .error_code = response.error_code,
                .error_message = response.error_message,
            };
        }
        if (response.error_code == "main_thread_timeout")
        {
            return {
                .state = ServerProbeState::Busy,
                .error_code = response.error_code,
                .error_message = response.error_message,
            };
        }
        return unavailable_result(evidence,
            response.error_code, response.error_message);
    }

    std::string parse_error;
    auto welcome = server_welcome_from_json(response.result, parse_error);
    if (!welcome)
    {
        return {
            .state = ServerProbeState::Stale,
            .error_code = "invalid_welcome",
            .error_message = std::move(parse_error),
        };
    }
    if (welcome->protocol_major != options.protocol_major)
    {
        return {
            .state = ServerProbeState::Incompatible,
            .error_code = "incompatible_protocol",
            .error_message = "Client/server protocol major versions do not match.",
        };
    }
    return {
        .state = ServerProbeState::Ready,
        .welcome = std::move(welcome),
    };
}

ServerProbeResult ServerClient::ensure(const ServerEnsureOptions& options)
{
    ServerEnsureOptions effective = options;
    if (effective.registration_nonce.empty())
        effective.registration_nonce = make_server_client_id();
    ServerProbeResult current = probe(effective);
    if (current.ready()
        || current.state == ServerProbeState::Incompatible
        || current.error_code == "runtime_unavailable"
        || !options.launch_if_missing)
    {
        return current;
    }

    bool launched = false;
    if (current.state == ServerProbeState::Absent
        || current.state == ServerProbeState::Crashed
        || current.state == ServerProbeState::Stale)
    {
        std::string launch_error;
        if (!launch_detached(effective, launch_error))
        {
            return {
                .state = ServerProbeState::LaunchFailed,
                .error_code = "launch_failed",
                .error_message = std::move(launch_error),
            };
        }
        launched = true;
    }

    const auto deadline
        = std::chrono::steady_clock::now() + effective.timeout;
    do
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        current = probe(effective);
        if (current.ready()
            || current.state == ServerProbeState::Incompatible
            || current.error_code == "runtime_unavailable")
        {
            return current;
        }
        if (!launched
            && (current.state == ServerProbeState::Absent
                || current.state == ServerProbeState::Crashed
                || current.state == ServerProbeState::Stale))
        {
            std::string launch_error;
            if (!launch_detached(effective, launch_error))
            {
                return {
                    .state = ServerProbeState::LaunchFailed,
                    .error_code = "launch_failed",
                    .error_message = std::move(launch_error),
                };
            }
            launched = true;
        }
    } while (std::chrono::steady_clock::now() < deadline);

    if (current.error_message.empty())
        current.error_message = "Timed out waiting for the Draxul server.";
    if (current.error_code.empty())
        current.error_code = "startup_timeout";
    return current;
}

ServerStatusResult ServerClient::status(
    const std::filesystem::path& runtime_directory,
    std::chrono::milliseconds request_timeout)
{
    const auto response = ControlClient::request(
        namespaced_control_id(kServerControlId, runtime_directory),
        runtime_directory, "server.status",
        nlohmann::json::object(),
        { .timeout = request_timeout });
    if (!response.ok)
    {
        return {
            .error_code = response.error_code,
            .error_message = response.error_message,
        };
    }
    std::string parse_error;
    auto status = server_status_from_json(response.result, parse_error);
    if (!status)
    {
        return {
            .error_code = "invalid_status",
            .error_message = std::move(parse_error),
        };
    }
    return { .ok = true, .status = std::move(status) };
}

namespace
{

// Session-mutating server methods can collide with an in-flight checkpoint
// write; the server answers "checkpoint_busy" while it finishes. Retry
// briefly instead of surfacing that transient state, and map
// "unknown_method" from older servers to stable guidance naming the
// unsupported operation.
bool request_with_checkpoint_retry(
    const std::filesystem::path& runtime_directory,
    std::string_view method, const nlohmann::json& params,
    std::string_view unsupported_noun, std::string& error)
{
    ControlClientResult response;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    do
    {
        response = ControlClient::request(
            namespaced_control_id(
                kServerControlId, runtime_directory),
            runtime_directory, method, params);
        if (response.ok
            || response.error_code != "checkpoint_busy")
        {
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(25));
    } while (std::chrono::steady_clock::now() < deadline);
    if (!response.ok)
    {
        error = response.error_code == "unknown_method"
            ? "The running Draxul server does not support "
                + std::string(unsupported_noun)
                + ". Stop it and retry."
            : response.error_message;
        return false;
    }
    error.clear();
    return true;
}

} // namespace

bool ServerClient::delete_session(
    const std::filesystem::path& runtime_directory,
    std::string_view session_id,
    const ServerDeleteSessionOptions& options,
    std::string& error)
{
    return request_with_checkpoint_retry(runtime_directory,
        "server.delete_session",
        {
            { "session_id", session_id },
            { "confirm_live_terminals",
                options.confirm_live_terminals },
        },
        "Session deletion", error);
}

bool ServerClient::delete_all_sessions(
    const std::filesystem::path& runtime_directory,
    const ServerDeleteSessionOptions& options,
    std::string& error)
{
    return request_with_checkpoint_retry(runtime_directory,
        "server.delete_all_sessions",
        {
            { "confirm_live_terminals",
                options.confirm_live_terminals },
        },
        "bulk Session deletion", error);
}

bool ServerClient::rename_session(
    const std::filesystem::path& runtime_directory,
    std::string_view session_id,
    std::string_view session_name,
    std::string& error)
{
    return request_with_checkpoint_retry(runtime_directory,
        "server.rename_session",
        {
            { "session_id", session_id },
            { "session_name", session_name },
        },
        "Session renaming", error);
}

bool ServerClient::shutdown(
    const std::filesystem::path& runtime_directory,
    const ServerShutdownOptions& options, std::string& error)
{
    const auto response = ControlClient::request(
        namespaced_control_id(kServerControlId, runtime_directory),
        runtime_directory, "server.shutdown",
        { { "confirm_live_terminals",
            options.confirm_live_terminals } },
        { .timeout = options.request_timeout });
    if (!response.ok)
    {
        error = response.error_message;
        return false;
    }
    return true;
}

bool ServerClient::disconnect(
    const std::filesystem::path& runtime_directory,
    std::string_view client_id, std::string& error,
    std::string_view connection_token)
{
    if (client_id.empty() || client_id.size() > kServerMaxClientIdBytes)
    {
        error = "A valid client identity is required.";
        return false;
    }
    nlohmann::json params{
        { "client_id", client_id },
    };
    if (!connection_token.empty())
        params["connection_token"] = connection_token;
    const auto response = ControlClient::request(
        namespaced_control_id(kServerControlId, runtime_directory),
        runtime_directory, "server.goodbye",
        std::move(params));
    if (!response.ok)
    {
        error = response.error_message;
        return false;
    }
    return true;
}

bool ServerClient::force_stop(
    const std::filesystem::path& runtime_directory,
    bool confirmed, std::string& error)
{
    if (!confirmed)
    {
        error = "Force stop requires explicit confirmation.";
        return false;
    }
    RuntimeEvidence identity = inspect_runtime(runtime_directory);
    if (!identity.metadata_valid
        || !identity.metadata_process_matches)
    {
        error = "No live Draxul server process identity is published.";
        return false;
    }

    // Prefer a live status response, but never make emergency recovery depend
    // on the main loop it exists to recover. The filesystem identity is
    // sufficient when the bounded health check cannot complete.
    const auto initial = status(
        runtime_directory, std::chrono::milliseconds(250));
    if (initial.ok && initial.status
        && (initial.status->server_pid != identity.metadata_pid
            || (!identity.metadata_epoch.empty()
                && initial.status->server_epoch
                    != identity.metadata_epoch)))
    {
        error = "The Draxul server identity does not match its published metadata.";
        return false;
    }
    const uint64_t pid = identity.metadata_pid;
    const std::string start_token
        = identity.metadata_start_token;
    const std::string epoch = identity.metadata_epoch;

#ifdef _WIN32
    if (pid == 0 || pid > std::numeric_limits<DWORD>::max())
    {
        error = "The Draxul server reported an invalid process identity.";
        return false;
    }
    // Hold the process object across the final identity check. A Windows
    // handle continues to name the same process even if its numeric PID is
    // reused.
    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE
            | SYNCHRONIZE,
        FALSE,
        static_cast<DWORD>(pid));
    if (!process)
    {
        error = "Unable to open the Draxul server process (error "
            + std::to_string(GetLastError()) + ").";
        return false;
    }
    const auto handle_start_token
        = process_start_token(process);
    if (!handle_start_token
        || *handle_start_token != start_token)
    {
        CloseHandle(process);
        error = "The Draxul server process identity changed before force stop.";
        return false;
    }
#else
    if (pid == 0 || pid > static_cast<uint64_t>(std::numeric_limits<pid_t>::max()))
    {
        error = "The Draxul server reported an invalid process identity.";
        return false;
    }
#endif

    // Re-read the filesystem identity immediately before terminating. Unlike
    // status(), this remains available when the server main loop is wedged.
    const RuntimeEvidence current
        = inspect_runtime(runtime_directory);
    if (!current.metadata_valid
        || current.metadata_pid != pid
        || current.metadata_start_token != start_token
        || (!epoch.empty() && current.metadata_epoch != epoch)
        || !current.metadata_process_matches)
    {
        error = "The Draxul server changed while force stop was being confirmed.";
#ifdef _WIN32
        CloseHandle(process);
#endif
        return false;
    }
#ifdef _WIN32
    const bool stopped = TerminateProcess(process, 2) != FALSE;
    const DWORD terminate_error = stopped ? ERROR_SUCCESS : GetLastError();
    if (stopped)
        WaitForSingleObject(process, 2000);
    CloseHandle(process);
    if (!stopped)
    {
        error = "Unable to force stop the Draxul server (error "
            + std::to_string(terminate_error) + ").";
        return false;
    }
#else
    if (::kill(static_cast<pid_t>(pid), SIGKILL) != 0)
    {
        error = "Unable to force stop the Draxul server.";
        return false;
    }
#endif
    return true;
}

bool ServerClient::launch_detached(
    const ServerEnsureOptions& options, std::string& error)
{
    const auto& executable_path = options.executable_path;
    const auto& runtime_directory = options.runtime_directory;
    if (executable_path.empty() || !std::filesystem::exists(executable_path))
    {
        error = "Draxul executable path does not exist.";
        return false;
    }
#ifdef _WIN32
    std::filesystem::path server_executable;
    if (!prepare_windows_server_helper(
            executable_path, server_executable, error))
    {
        return false;
    }
    const std::wstring executable
        = server_executable.wstring();
    std::wstring command = quote_windows_arg(executable)
        + L" --server --server-runtime-dir "
        + quote_windows_arg(runtime_directory.wstring());
    if (!options.terminal_shell_kind.empty())
    {
        command += L" --server-shell "
            + quote_windows_arg(std::filesystem::path(
                options.terminal_shell_kind)
                    .wstring());
    }
    if (!options.terminal_command.empty())
    {
        command += L" --server-command "
            + quote_windows_arg(
                std::filesystem::path(options.terminal_command)
                    .wstring());
    }
    if (!options.terminal_working_directory.empty())
    {
        command += L" --server-working-dir "
            + quote_windows_arg(
                options.terminal_working_directory.wstring());
    }
    command += L" --server-scrollback-lines "
        + std::to_wstring(options.terminal_scrollback_lines);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    const DWORD flags
        = CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    if (!CreateProcessW(executable.c_str(), mutable_command.data(),
            nullptr, nullptr, FALSE, flags, nullptr, nullptr,
            &startup, &process))
    {
        error = "Unable to launch the Draxul server (error "
            + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    // Thin wrapper over draxul::spawn_detached, which carries the double-fork
    // + pre-built-argv + /dev/null machinery (and its rationale) for every
    // detached launch in the tree.
    std::vector<std::filesystem::path> arguments{
        "--server",
        "--server-runtime-dir",
        runtime_directory,
    };
    if (!options.terminal_shell_kind.empty())
    {
        arguments.emplace_back("--server-shell");
        arguments.emplace_back(options.terminal_shell_kind);
    }
    if (!options.terminal_command.empty())
    {
        arguments.emplace_back("--server-command");
        arguments.emplace_back(options.terminal_command);
    }
    if (!options.terminal_working_directory.empty())
    {
        arguments.emplace_back("--server-working-dir");
        arguments.emplace_back(options.terminal_working_directory);
    }
    arguments.emplace_back("--server-scrollback-lines");
    arguments.emplace_back(
        std::to_string(options.terminal_scrollback_lines));
    if (!spawn_detached(executable_path, arguments, {}, error))
    {
        error = "Unable to fork the Draxul server process.";
        return false;
    }
    return true;
#endif
}

} // namespace draxul
