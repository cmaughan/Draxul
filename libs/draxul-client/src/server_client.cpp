#include <draxul/server_client.h>

#include <draxul/control_plane.h>

#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
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

uint64_t current_unix_time_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

#ifdef _WIN32
std::optional<std::string> process_start_token(HANDLE process)
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        return std::nullopt;
    const uint64_t value
        = (static_cast<uint64_t>(created.dwHighDateTime) << 32)
        | created.dwLowDateTime;
    return std::to_string(value);
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
    const auto token = alive
        ? process_start_token(process)
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
        + ":" + std::to_string(
              process.kp_proc.p_starttime.tv_usec);
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
};

RuntimeEvidence inspect_runtime(const std::filesystem::path& runtime_directory)
{
    RuntimeEvidence evidence;
    const auto metadata_path = server_metadata_path(runtime_directory);
    evidence.metadata_exists = std::filesystem::exists(metadata_path);
    if (const auto metadata = read_bounded_json(metadata_path))
    {
        evidence.metadata_version_mismatch
            = metadata->contains("version")
            && (*metadata)["version"].is_number_integer()
            && (*metadata)["version"].get<int>()
                != kControlProtocolVersion;
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

    const uint64_t now = current_unix_time_ms();
    std::error_code iteration_error;
    if (!std::filesystem::is_directory(runtime_directory, iteration_error)
        || iteration_error)
        return evidence;
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

#ifdef _WIN32

std::wstring quote_windows_argument(const std::wstring& argument)
{
    if (argument.find_first_of(L" \t\"") == std::wstring::npos)
        return argument;
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const wchar_t ch : argument)
    {
        if (ch == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (ch == L'"')
        {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

#endif

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
    if (!evidence.metadata_exists)
        return unavailable_result(evidence, "endpoint_unavailable",
            "No Draxul server endpoint is published.");

    ServerHello hello{
        .protocol_major = options.protocol_major,
        .protocol_minor = options.protocol_minor,
        .client_id = options.client_id.empty()
            ? make_server_client_id()
            : options.client_id,
        .capabilities = {
            "agent-control-v1",
            "agent-projection-v1",
            "client-registration",
            "controller-lease",
            "fake-remote-terminal",
            "graceful-shutdown",
            "managed-agent-v1",
            "multi-terminal-v1",
            "named-sessions-v1",
            "ordered-terminal-events",
            "real-remote-terminal",
            "session-delete-v1",
            "status",
            "terminal-metrics-v1",
            "terminal-scrollback-v1",
            "terminal-uncompressed-v1",
            "topology-v1",
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
    ServerProbeResult current = probe(options);
    if (current.ready()
        || current.state == ServerProbeState::Incompatible
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
        if (!launch_detached(options, launch_error))
        {
            return {
                .state = ServerProbeState::LaunchFailed,
                .error_code = "launch_failed",
                .error_message = std::move(launch_error),
            };
        }
        launched = true;
    }

    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    do
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        current = probe(options);
        if (current.ready()
            || current.state == ServerProbeState::Incompatible)
        {
            return current;
        }
        if (!launched
            && (current.state == ServerProbeState::Absent
                || current.state == ServerProbeState::Crashed
                || current.state == ServerProbeState::Stale))
        {
            std::string launch_error;
            if (!launch_detached(options, launch_error))
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

bool ServerClient::delete_session(
    const std::filesystem::path& runtime_directory,
    std::string_view session_id,
    const ServerDeleteSessionOptions& options,
    std::string& error)
{
    ControlClientResult response;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    do
    {
        response = ControlClient::request(
            namespaced_control_id(
                kServerControlId, runtime_directory),
            runtime_directory, "server.delete_session",
            {
                { "session_id", session_id },
                { "confirm_live_terminals",
                    options.confirm_live_terminals },
            });
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
              "Session deletion. Stop it and retry."
            : response.error_message;
        return false;
    }
    error.clear();
    return true;
}

bool ServerClient::rename_session(
    const std::filesystem::path& runtime_directory,
    std::string_view session_id,
    std::string_view session_name,
    std::string& error)
{
    ControlClientResult response;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    do
    {
        response = ControlClient::request(
            namespaced_control_id(
                kServerControlId, runtime_directory),
            runtime_directory, "server.rename_session",
            {
                { "session_id", session_id },
                { "session_name", session_name },
            });
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
              "Session renaming. Stop it and retry."
            : response.error_message;
        return false;
    }
    error.clear();
    return true;
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
    std::string_view client_id, std::string& error)
{
    if (client_id.empty() || client_id.size() > kServerMaxClientIdBytes)
    {
        error = "A valid client identity is required.";
        return false;
    }
    const auto response = ControlClient::request(
        namespaced_control_id(kServerControlId, runtime_directory),
        runtime_directory, "server.goodbye",
        { { "client_id", client_id } });
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
    if (pid == 0 || pid > static_cast<uint64_t>(
                              std::numeric_limits<pid_t>::max()))
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
    const std::wstring executable = executable_path.wstring();
    std::wstring command = quote_windows_argument(executable)
        + L" --server --server-runtime-dir "
        + quote_windows_argument(runtime_directory.wstring());
    if (!options.terminal_shell_kind.empty())
    {
        command += L" --server-shell "
            + quote_windows_argument(std::filesystem::path(
                  options.terminal_shell_kind)
                                         .wstring());
    }
    if (!options.terminal_command.empty())
    {
        command += L" --server-command "
            + quote_windows_argument(
                std::filesystem::path(options.terminal_command)
                    .wstring());
    }
    if (!options.terminal_working_directory.empty())
    {
        command += L" --server-working-dir "
            + quote_windows_argument(
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
    const pid_t child = ::fork();
    if (child < 0)
    {
        error = "Unable to fork the Draxul server process.";
        return false;
    }
    if (child == 0)
    {
        ::setsid();
        const int null_fd = ::open("/dev/null", O_RDWR);
        if (null_fd >= 0)
        {
            ::dup2(null_fd, STDIN_FILENO);
            ::dup2(null_fd, STDOUT_FILENO);
            ::dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO)
                ::close(null_fd);
        }
        const std::string executable = executable_path.string();
        const std::string runtime = runtime_directory.string();
        const std::string scrollback
            = std::to_string(options.terminal_scrollback_lines);
        std::vector<std::string> arguments{
            executable,
            "--server",
            "--server-runtime-dir",
            runtime,
        };
        if (!options.terminal_shell_kind.empty())
        {
            arguments.push_back("--server-shell");
            arguments.push_back(options.terminal_shell_kind);
        }
        if (!options.terminal_command.empty())
        {
            arguments.push_back("--server-command");
            arguments.push_back(options.terminal_command);
        }
        if (!options.terminal_working_directory.empty())
        {
            arguments.push_back("--server-working-dir");
            arguments.push_back(
                options.terminal_working_directory.string());
        }
        arguments.push_back("--server-scrollback-lines");
        arguments.push_back(scrollback);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (std::string& argument : arguments)
            argv.push_back(argument.data());
        argv.push_back(nullptr);
        ::execv(executable.c_str(), argv.data());
        _exit(127);
    }
    return true;
#endif
}

} // namespace draxul
