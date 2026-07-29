#include <draxul/server_client.h>

#include <draxul/control_plane.h>

#include <fstream>
#include <iomanip>
#include <iterator>
#include <nlohmann/json.hpp>
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
#include <unistd.h>
#endif

namespace draxul
{

namespace
{

constexpr std::string_view kStartingMarkerPrefix = "server-starting-";

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

bool process_is_alive(uint64_t pid)
{
    if (pid == 0)
        return false;
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
        static_cast<DWORD>(pid));
    if (!process)
        return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code)
        && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    const int result = ::kill(static_cast<pid_t>(pid), 0);
    return result == 0 || errno == EPERM;
#endif
}

struct RuntimeEvidence
{
    bool metadata_exists = false;
    bool metadata_valid = false;
    uint64_t metadata_pid = 0;
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
        evidence.metadata_valid = metadata->contains("server_pid")
            && (*metadata)["server_pid"].is_number_unsigned();
        if (evidence.metadata_valid)
            evidence.metadata_pid = (*metadata)["server_pid"].get<uint64_t>();
    }

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
        if (process_is_alive(marker_pid))
            evidence.live_starting_process = true;
        else
            evidence.stale_starting_marker = true;
    }
    return evidence;
}

ServerProbeResult unavailable_result(
    const RuntimeEvidence& evidence,
    std::string error_code,
    std::string error_message)
{
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
        return {
            .state = process_is_alive(evidence.metadata_pid)
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
            "client-registration",
            "controller-lease",
            "fake-remote-terminal",
            "graceful-shutdown",
            "multi-terminal-v1",
            "named-sessions-v1",
            "ordered-terminal-events",
            "real-remote-terminal",
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
        "server.hello", server_hello_to_json(hello));
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
    const std::filesystem::path& runtime_directory)
{
    const auto response = ControlClient::request(
        namespaced_control_id(kServerControlId, runtime_directory),
        runtime_directory, "server.status");
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

bool ServerClient::shutdown(
    const std::filesystem::path& runtime_directory, std::string& error)
{
    const auto response = ControlClient::request(
        namespaced_control_id(kServerControlId, runtime_directory),
        runtime_directory, "server.shutdown");
    if (!response.ok)
    {
        error = response.error_message;
        return false;
    }
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
