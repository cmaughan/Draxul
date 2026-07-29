#include <draxul/server_kernel.h>

#include "fake_terminal_runtime.h"
#include "remote_terminal_service.h"
#include "server_terminal_runtime.h"
#include "session_topology_bridge.h"
#include "topology_service.h"

#include <draxul/control_plane.h>
#include <draxul/log.h>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_agent_service.h>
#include <draxul/server_protocol.h>
#include <draxul/session_state.h>

#include <algorithm>
#include <charconv>
#include <deque>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace draxul
{

namespace
{

// Leave room for the control response envelope and JSON framing. Resize
// deltas contain a complete grid and can be several MiB apiece, so an event
// count alone is not a safe batching limit.
constexpr size_t kRemoteTerminalPollPayloadBudget
    = kControlMaxMessageBytes - 64 * 1024;

uint64_t current_process_id()
{
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(::getpid());
#endif
}

uint64_t current_unix_time_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string random_epoch()
{
    std::random_device random;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int index = 0; index < 16; ++index)
        out << std::setw(2) << (random() & 0xff);
    return out.str();
}

std::filesystem::path starting_marker_path(
    const std::filesystem::path& runtime_directory, uint64_t pid)
{
    return runtime_directory / ("server-starting-" + std::to_string(pid) + ".json");
}

uint64_t numeric_suffix(std::string_view value)
{
    const size_t separator = value.find_last_of('-');
    if (separator == std::string_view::npos
        || separator + 1 >= value.size())
    {
        return 0;
    }
    uint64_t parsed = 0;
    const char* begin = value.data() + separator + 1;
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    return result.ec == std::errc{} && result.ptr == end
        ? parsed
        : 0;
}

bool valid_server_session_id(std::string_view value)
{
    return !value.empty()
        && value.size() <= kServerMaxSessionIdBytes
        && !std::ranges::any_of(value,
            [](unsigned char ch) {
                return ch < 0x20 || ch == 0x7f;
            });
}

const std::vector<std::string>& server_capabilities()
{
    static const std::vector<std::string> capabilities{
        "client-registration",
        "controller-lease",
        "agent-control-v1",
        "agent-projection-v1",
        "fake-remote-terminal",
        "graceful-shutdown",
        "multi-terminal-v1",
        "named-sessions-v1",
        "ordered-terminal-events",
        "real-remote-terminal",
        "session-persistence-v1",
        "status",
        "terminal-metrics-v1",
        "terminal-scrollback-v1",
        "terminal-uncompressed-v1",
        "topology-v1",
    };
    return capabilities;
}

std::vector<std::string> negotiate_capabilities(
    const std::vector<std::string>& requested)
{
    const std::unordered_set<std::string> supported(
        server_capabilities().begin(), server_capabilities().end());
    std::vector<std::string> result;
    for (const std::string& capability : requested)
    {
        if (supported.contains(capability))
            result.push_back(capability);
    }
    return result;
}

} // namespace

std::filesystem::path server_session_state_path(
    const std::filesystem::path& runtime_directory)
{
    return runtime_directory / "sessions" / "default.toml";
}

std::filesystem::path server_session_state_path(
    const std::filesystem::path& runtime_directory,
    std::string_view session_id)
{
    if (session_id.empty() || session_id == "default")
        return server_session_state_path(runtime_directory);
    return runtime_directory / "sessions"
        / session_state_file_name(session_id);
}

class ServerKernel::Impl
{
public:
    struct ServerSession;

    explicit Impl(ServerKernelOptions value)
        : options(std::move(value))
    {
        if (options.protocol_major < 0)
            options.protocol_major = kServerProtocolMajor;
        if (options.protocol_minor < 0)
            options.protocol_minor = kServerProtocolMinor;
        if (options.build_version.empty())
            options.build_version = server_build_version();
        epoch_value = options.epoch_override.empty()
            ? random_epoch()
            : options.epoch_override;
        pid = current_process_id();
    }

    ServerStartResult start();
    int run_until_stopped();
    void request_stop();
    void stop();
    ControlMethodResult handle_request(const ControlRequest& request);
    ControlMethodResult attach_fake_terminal(const nlohmann::json& params);
    ControlMethodResult poll_fake_terminal(const nlohmann::json& params);
    ControlMethodResult input_fake_terminal(const nlohmann::json& params);
    ControlMethodResult resize_fake_terminal(const nlohmann::json& params);
    ControlMethodResult take_fake_terminal_control(const nlohmann::json& params);
    ControlMethodResult disconnect_fake_terminal(const nlohmann::json& params);
    ServerStatusSnapshot status_snapshot() const;
    void publish_starting_marker();
    void remove_starting_marker();
    void remove_all_starting_markers();
    bool prepare_session_restore(std::string& error);
    bool initialize_services(std::string& error);
    bool initialize_session(
        std::string_view session_id, std::string& error);
    ServerSession* ensure_session(
        std::string_view session_id, std::string& error);
    void reset_services();
    bool checkpoint_session(
        std::string_view session_id, std::string& error);
    bool read_session_id(const nlohmann::json& params,
        std::string& session_id, std::string& error) const;
    std::optional<std::string> create_server_terminal(
        std::string_view session_id, std::string_view pane_id,
        std::string_view name,
        std::string& error);
    bool create_server_terminal_with_id(std::string_view session_id,
        std::string terminal_id,
        std::string_view pane_id, std::string_view name,
        std::string& error);
    void destroy_server_terminal(std::string_view session_id,
        std::string_view terminal_id);
    bool restart_server_terminal(std::string_view session_id,
        std::string_view terminal_id, std::string& error);
    void refresh_agents(ServerSession& session,
        std::chrono::steady_clock::time_point now);

    ServerKernelOptions options;
    ControlServer control;
    std::string epoch_value;
    uint64_t pid = 0;
    std::chrono::steady_clock::time_point started_at{};
    std::atomic<bool> started = false;
    std::atomic<bool> stop_requested = false;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> clients;
    std::filesystem::path starting_marker;

    struct FakeSubscriber
    {
        std::deque<RemoteTerminalEvent> events;
        bool needs_resync = false;
    };

    bool read_fake_client_id(
        const nlohmann::json& params, std::string& client_id) const;
    bool read_fake_version(
        const nlohmann::json& params, RemoteTerminalVersion& version) const;
    RemoteTerminalVersion fake_version() const;
    RemoteTerminalEvent fake_snapshot_event() const;
    RemoteTerminalEvent make_fake_delta_event();
    RemoteTerminalEvent make_fake_controller_event();
    void broadcast_fake_event(const RemoteTerminalEvent& event);

    std::unique_ptr<FakeTerminalRuntime> fake_terminal;
    uint64_t fake_generation = 1;
    uint64_t fake_sequence = 0;
    std::string fake_controller_client_id;
    std::unordered_map<std::string, FakeSubscriber> fake_subscribers;
    struct ServerTerminalEndpoint
    {
        std::unique_ptr<ServerTerminalRuntime> runtime;
        std::unique_ptr<RemoteTerminalService> service;
    };
    struct ServerSession
    {
        std::string session_id;
        std::unique_ptr<TopologyService> topology_service;
        std::unique_ptr<ServerAgentService> agent_service;
        std::unordered_map<std::string, ServerTerminalEndpoint>
            terminals;
        uint64_t next_terminal_serial = 2;
        std::filesystem::path persistence_path;
        bool checkpoint_enabled = true;
        std::vector<std::string> restore_warnings;
        std::optional<TopologySnapshot> restored_topology;
        std::string checkpoint_state = "pending";
        std::string checkpoint_error;
        uint64_t last_checkpoint_unix_ms = 0;
        uint64_t last_checkpoint_revision = 0;
        bool checkpoint_file_present = false;
        std::chrono::steady_clock::time_point next_checkpoint_at{};
        std::chrono::steady_clock::time_point next_agent_refresh_at{};
    };
    std::unordered_map<std::string, std::unique_ptr<ServerSession>>
        sessions;
    std::vector<std::string> unassigned_restore_warnings;
};

void ServerKernel::Impl::publish_starting_marker()
{
    std::error_code directory_error;
    std::filesystem::create_directories(options.runtime_directory, directory_error);
#ifndef _WIN32
    if (!directory_error)
        ::chmod(options.runtime_directory.c_str(), 0700);
#endif
    starting_marker = starting_marker_path(options.runtime_directory, pid);
    std::ofstream output(starting_marker, std::ios::binary | std::ios::trunc);
    if (output)
    {
        output << nlohmann::json{
            { "pid", pid },
            { "epoch", epoch_value },
            { "protocol_major", options.protocol_major },
        }
                      .dump();
        output.flush();
    }
}

void ServerKernel::Impl::remove_starting_marker()
{
    if (starting_marker.empty())
        return;
    std::error_code ignored;
    std::filesystem::remove(starting_marker, ignored);
    starting_marker.clear();
}

void ServerKernel::Impl::remove_all_starting_markers()
{
    std::error_code iteration_error;
    for (std::filesystem::directory_iterator it(
             options.runtime_directory, iteration_error);
         !iteration_error && it != std::filesystem::directory_iterator();
         it.increment(iteration_error))
    {
        const std::string name = it->path().filename().string();
        if (!name.starts_with("server-starting-")
            || it->path().extension() != ".json")
        {
            continue;
        }
        std::error_code remove_error;
        std::filesystem::remove(it->path(), remove_error);
    }
    starting_marker.clear();
}

bool ServerKernel::Impl::create_server_terminal_with_id(
    std::string_view session_id, std::string terminal_id,
    std::string_view pane_id, std::string_view name,
    std::string& error)
{
    const auto found = sessions.find(std::string(session_id));
    if (found == sessions.end())
    {
        error = "The requested server Session does not exist.";
        return false;
    }
    ServerSession& session = *found->second;
    if (terminal_id.empty() || pane_id.empty())
    {
        error = "Server terminal identity is incomplete.";
        return false;
    }
    auto runtime = std::make_unique<ServerTerminalRuntime>(
        ServerTerminalRuntimeOptions{
            .shell_kind = options.terminal_shell_kind,
            .command = options.terminal_command,
            .args = options.terminal_args,
            .working_directory = options.terminal_working_directory,
            .environment = options.terminal_environment,
            .scrollback_capacity = options.terminal_scrollback_lines,
        });
    auto service = std::make_unique<RemoteTerminalService>(
        RemoteTerminalServiceOptions{
            .method_prefix = "terminal",
            .server_epoch = epoch_value,
            .pane_id = std::string(pane_id),
            .terminal_id = terminal_id,
            .name = name.empty()
                ? "Server Shell"
                : std::string(name),
        },
        *runtime);
    if (!session.terminals.emplace(
                              terminal_id,
                              ServerTerminalEndpoint{
                                  .runtime = std::move(runtime),
                                  .service = std::move(service),
                              })
             .second)
    {
        error = "Saved Session contains a duplicate server terminal identity.";
        return false;
    }
    session.next_terminal_serial = std::max(
        session.next_terminal_serial, numeric_suffix(terminal_id) + 1);
    return true;
}

void ServerKernel::Impl::reset_services()
{
    for (auto& [session_id, session] : sessions)
    {
        (void)session_id;
        session->topology_service.reset();
        session->agent_service.reset();
        session->terminals.clear();
        session->next_terminal_serial = 2;
    }
    fake_terminal.reset();
}

bool ServerKernel::Impl::prepare_session_restore(std::string&)
{
    sessions.clear();
    unassigned_restore_warnings.clear();

    auto prepare = [this](std::filesystem::path path,
                       std::optional<std::string> expected_session_id) {
        auto prepared = std::make_unique<ServerSession>();
        prepared->persistence_path = std::move(path);
        prepared->checkpoint_file_present
            = std::filesystem::exists(prepared->persistence_path);
        prepared->checkpoint_state = prepared->checkpoint_file_present
            ? "restoring"
            : "pending";

        std::optional<SessionSnapshot> saved;
        std::string load_error;
        if (prepared->checkpoint_file_present)
        {
            saved = load_session_state_from_path(
                prepared->persistence_path, &load_error);
        }

        if (expected_session_id)
            prepared->session_id = *expected_session_id;
        else if (saved)
            prepared->session_id = saved->session_id;

        if (!valid_server_session_id(prepared->session_id))
        {
            unassigned_restore_warnings.push_back(
                "Skipped Session checkpoint with an invalid identity: "
                + prepared->persistence_path.string());
            return;
        }

        if (saved && expected_session_id
            && saved->session_id != *expected_session_id)
        {
            load_error = "Saved Session identity does not match its checkpoint path.";
            saved.reset();
        }

        if (prepared->checkpoint_file_present && !saved)
        {
            prepared->checkpoint_enabled = false;
            prepared->checkpoint_state = "disabled";
            prepared->checkpoint_error = load_error.empty()
                ? "Saved Session could not be restored."
                : load_error;
            prepared->restore_warnings.push_back(
                prepared->checkpoint_error);
        }
        else if (saved)
        {
            auto restored
                = restore_session_topology(*saved, load_error);
            if (!restored)
            {
                prepared->checkpoint_enabled = false;
                prepared->checkpoint_state = "disabled";
                prepared->checkpoint_error = load_error.empty()
                    ? "Saved Session could not be restored."
                    : load_error;
                prepared->restore_warnings.push_back(
                    prepared->checkpoint_error);
            }
            else
            {
                prepared->restore_warnings
                    = std::move(restored->warnings);
                if (!prepared->restore_warnings.empty())
                {
                    prepared->checkpoint_enabled = false;
                    prepared->checkpoint_state = "disabled";
                    prepared->checkpoint_error
                        = "Checkpoint disabled after partial restore.";
                }
                else
                {
                    prepared->checkpoint_state = "restored";
                }
                prepared->restored_topology
                    = std::move(restored->topology);
            }
        }

        const std::string session_id = prepared->session_id;
        if (!sessions.emplace(session_id, std::move(prepared)).second)
        {
            unassigned_restore_warnings.push_back(
                "Skipped duplicate Session checkpoint for '"
                + session_id + "'.");
            return;
        }
        const ServerSession& stored = *sessions.at(session_id);
        if (!stored.restore_warnings.empty())
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Draxul server retained Session checkpoint %s with restore warnings: %s",
                stored.persistence_path.string().c_str(),
                stored.restore_warnings.front().c_str());
        }
    };

    const std::filesystem::path default_path
        = options.session_state_file.empty()
        ? server_session_state_path(options.runtime_directory)
        : options.session_state_file;
    prepare(default_path, std::string("default"));

    const std::filesystem::path sessions_directory
        = options.runtime_directory / "sessions";
    std::vector<std::filesystem::path> saved_paths;
    std::error_code iteration_error;
    for (std::filesystem::directory_iterator it(
             sessions_directory, iteration_error);
         !iteration_error
         && it != std::filesystem::directory_iterator();
         it.increment(iteration_error))
    {
        if (it->is_regular_file()
            && it->path().extension() == ".toml"
            && it->path().lexically_normal()
                != default_path.lexically_normal())
        {
            saved_paths.push_back(it->path());
        }
    }
    std::ranges::sort(saved_paths);
    for (const auto& path : saved_paths)
        prepare(path, std::nullopt);
    return true;
}

bool ServerKernel::Impl::initialize_session(
    std::string_view session_id, std::string& error)
{
    const auto found = sessions.find(std::string(session_id));
    if (found == sessions.end())
    {
        error = "The requested server Session does not exist.";
        return false;
    }
    ServerSession& session = *found->second;
    session.topology_service.reset();
    session.agent_service.reset();
    session.terminals.clear();
    session.next_terminal_serial = 2;

    const std::string stable_session_id = session.session_id;
    TopologyServiceCallbacks callbacks{
        .create_server_terminal
        = [this, stable_session_id](std::string_view pane_id,
              std::string_view name, std::string& callback_error) {
              return create_server_terminal(stable_session_id,
                  pane_id, name, callback_error);
          },
        .destroy_server_terminal
        = [this, stable_session_id](std::string_view terminal_id) {
              destroy_server_terminal(
                  stable_session_id, terminal_id);
          },
        .restart_server_terminal
        = [this, stable_session_id](std::string_view terminal_id,
              std::string& callback_error) {
              return restart_server_terminal(stable_session_id,
                  terminal_id, callback_error);
          },
    };

    if (session.restored_topology)
    {
        for (const auto& space : session.restored_topology->spaces)
        {
            for (const auto& tab : space.tabs)
            {
                for (const auto& pane : tab.panes)
                {
                    if (pane.domain
                        != TopologyPaneDomain::ServerTerminal)
                    {
                        continue;
                    }
                    if (!create_server_terminal_with_id(
                            stable_session_id, pane.terminal_id,
                            pane.pane_id, pane.name, error))
                    {
                        session.terminals.clear();
                        return false;
                    }
                }
            }
        }
        session.topology_service
            = std::make_unique<TopologyService>(
                *session.restored_topology,
                std::move(callbacks));
        session.last_checkpoint_revision
            = session.topology_service->snapshot().revision;
    }
    else
    {
        if (!create_server_terminal_with_id(stable_session_id,
                std::string(kServerShellTerminalId),
                kServerShellPaneId, "Server Shell", error))
        {
            session.terminals.clear();
            return false;
        }
        session.topology_service
            = std::make_unique<TopologyService>(
                stable_session_id, std::move(callbacks));
    }
    session.next_checkpoint_at = std::chrono::steady_clock::now()
        + options.session_checkpoint_interval;
    session.agent_service
        = std::make_unique<ServerAgentService>(
            stable_session_id);
    session.next_agent_refresh_at
        = std::chrono::steady_clock::now();
    return true;
}

ServerKernel::Impl::ServerSession*
ServerKernel::Impl::ensure_session(
    std::string_view session_id, std::string& error)
{
    const auto found = sessions.find(std::string(session_id));
    if (found != sessions.end())
        return found->second.get();

    auto session = std::make_unique<ServerSession>();
    session->session_id = std::string(session_id);
    session->persistence_path = server_session_state_path(
        options.runtime_directory, session_id);
    const std::string key = session->session_id;
    sessions.emplace(key, std::move(session));
    if (!initialize_session(key, error))
    {
        sessions.erase(key);
        return nullptr;
    }
    return sessions.at(key).get();
}

bool ServerKernel::Impl::initialize_services(std::string& error)
{
    reset_services();
    fake_terminal = std::make_unique<FakeTerminalRuntime>();
    std::vector<std::string> session_ids;
    session_ids.reserve(sessions.size());
    for (const auto& [session_id, session] : sessions)
    {
        (void)session;
        session_ids.push_back(session_id);
    }
    std::ranges::sort(session_ids);
    for (const auto& session_id : session_ids)
    {
        if (!initialize_session(session_id, error))
        {
            reset_services();
            return false;
        }
    }
    return true;
}

bool ServerKernel::Impl::checkpoint_session(
    std::string_view session_id, std::string& error)
{
    const auto found = sessions.find(std::string(session_id));
    if (found == sessions.end())
    {
        error = "The requested server Session does not exist.";
        return false;
    }
    ServerSession& server_session = *found->second;
    auto fail = [&](std::string message,
                    std::string state = "failed") {
        server_session.checkpoint_state = std::move(state);
        server_session.checkpoint_error = message.size() > 4096
            ? message.substr(0, 4096)
            : message;
        error = server_session.checkpoint_error;
        return false;
    };
    if (!server_session.topology_service)
        return fail("Server topology is unavailable.");
    if (!server_session.checkpoint_enabled)
    {
        return fail(
            "Checkpoint disabled to preserve the previous Session file after restore warnings.",
            "disabled");
    }
    auto captured = capture_session_topology(
        server_session.topology_service->snapshot(), error);
    if (!captured)
        return fail(error);
    if (!save_session_state_to_path(*captured,
            server_session.persistence_path, &error))
    {
        return fail(error.empty()
                ? "Unable to save Session checkpoint."
                : error);
    }
    server_session.checkpoint_state = "ok";
    server_session.checkpoint_error.clear();
    server_session.last_checkpoint_unix_ms
        = current_unix_time_ms();
    server_session.last_checkpoint_revision
        = server_session.topology_service->snapshot().revision;
    server_session.checkpoint_file_present = true;
    error.clear();
    return true;
}

bool ServerKernel::Impl::read_session_id(
    const nlohmann::json& params, std::string& session_id,
    std::string& error) const
{
    session_id = "default";
    if (!params.is_object())
    {
        error = "Request parameters must be an object.";
        return false;
    }
    if (const auto value = params.find("session_id");
        value != params.end())
    {
        if (!value->is_string())
        {
            error = "Session identity must be a string.";
            return false;
        }
        session_id = value->get<std::string>();
    }
    if (!valid_server_session_id(session_id))
    {
        error = "Session identity is invalid.";
        return false;
    }
    return true;
}

ServerStartResult ServerKernel::Impl::start()
{
    if (started)
        return { ServerStartDisposition::Started, {} };
    if (options.runtime_directory.empty())
        return { ServerStartDisposition::Failed, "Server runtime directory is empty." };

    stop_requested = false;
    publish_starting_marker();
    std::string error;
    if (!prepare_session_restore(error))
    {
        remove_starting_marker();
        return { ServerStartDisposition::Failed, std::move(error) };
    }
    const nlohmann::json metadata{
        { "server_pid", pid },
        { "server_epoch", epoch_value },
        { "server_protocol_major", options.protocol_major },
        { "server_protocol_minor", options.protocol_minor },
        { "build_version", options.build_version },
        { "state", "ready" },
    };
    if (!control.start(
            namespaced_control_id(kServerControlId, options.runtime_directory),
            options.runtime_directory,
            [this]() { wake.notify_all(); }, &error, metadata))
    {
        remove_starting_marker();
        if (control.endpoint_in_use())
            return { ServerStartDisposition::AlreadyRunning, std::move(error) };
        return { ServerStartDisposition::Failed, std::move(error) };
    }

    started_at = std::chrono::steady_clock::now();
    started = true;
    // Once this process owns the singleton endpoint, no other starting marker
    // can represent a second valid owner. Clean crash leftovers and losing
    // concurrent launchers together.
    remove_all_starting_markers();
    DRAXUL_LOG_INFO(LogCategory::App,
        "Draxul server ready pid=%llu epoch=%s protocol=%d.%d",
        static_cast<unsigned long long>(pid),
        epoch_value.c_str(),
        options.protocol_major,
        options.protocol_minor);
    return { ServerStartDisposition::Started, {} };
}

ControlMethodResult ServerKernel::Impl::handle_request(
    const ControlRequest& request)
{
    if (request.method == "server.hello")
    {
        std::string parse_error;
        auto hello = server_hello_from_json(request.params, parse_error);
        if (!hello)
            return ControlMethodResult::error("invalid_hello", std::move(parse_error));
        if (hello->protocol_major != options.protocol_major)
        {
            return ControlMethodResult::error("incompatible_protocol",
                "Client/server protocol major versions do not match.");
        }

        {
            std::lock_guard guard(mutex);
            clients[hello->client_id] = std::chrono::steady_clock::now();
        }
        ServerWelcome welcome{
            .protocol_major = options.protocol_major,
            .protocol_minor = std::min(
                options.protocol_minor, hello->protocol_minor),
            .server_pid = pid,
            .server_epoch = epoch_value,
            .build_version = options.build_version,
            .capabilities = negotiate_capabilities(hello->capabilities),
        };
        return ControlMethodResult::success(server_welcome_to_json(welcome));
    }
    if (request.method == "server.status")
        return ControlMethodResult::success(server_status_to_json(status_snapshot()));
    if (request.method == "fake.attach")
        return attach_fake_terminal(request.params);
    if (request.method == "fake.poll")
        return poll_fake_terminal(request.params);
    if (request.method == "fake.input")
        return input_fake_terminal(request.params);
    if (request.method == "fake.resize")
        return resize_fake_terminal(request.params);
    if (request.method == "fake.take_control")
        return take_fake_terminal_control(request.params);
    if (request.method == "fake.disconnect")
        return disconnect_fake_terminal(request.params);
    if (request.method.starts_with("terminal."))
    {
        std::string session_id;
        std::string session_error;
        if (!read_session_id(
                request.params, session_id, session_error))
        {
            return ControlMethodResult::error(
                "invalid_session", std::move(session_error));
        }
        ServerSession* session
            = ensure_session(session_id, session_error);
        if (!session)
        {
            return ControlMethodResult::error(
                "session_unavailable", std::move(session_error));
        }
        std::string terminal_id
            = std::string(kServerShellTerminalId);
        if (request.params.is_object()
            && request.params.contains("terminal_id")
            && request.params["terminal_id"].is_string())
        {
            terminal_id
                = request.params["terminal_id"].get<std::string>();
        }
        const auto terminal = session->terminals.find(terminal_id);
        if (terminal == session->terminals.end())
        {
            return ControlMethodResult::error(
                "terminal_not_found",
                "The requested server terminal does not exist.");
        }
        return terminal->second.service->handle(
            request.method, request.params);
    }
    if (request.method == "topology.snapshot"
        || request.method == "topology.poll"
        || request.method == "topology.command")
    {
        std::string session_id;
        std::string session_error;
        if (!read_session_id(
                request.params, session_id, session_error))
        {
            return ControlMethodResult::error(
                "invalid_session", std::move(session_error));
        }
        ServerSession* session
            = ensure_session(session_id, session_error);
        if (!session || !session->topology_service)
        {
            return ControlMethodResult::error(
                "session_unavailable",
                session_error.empty()
                    ? "Server Session topology is unavailable."
                    : std::move(session_error));
        }
        return session->topology_service->handle(
            request.method, request.params);
    }
    if (request.method == "agent.snapshot"
        || request.method == "agent.poll"
        || request.method == "agent.list"
        || request.method == "agent.get"
        || request.method == "agent.explain"
        || request.method == "agent.wait"
        || request.method == "agent.restart"
        || request.method == "agent.send_text"
        || request.method == "agent.send_keys")
    {
        std::string session_id;
        std::string session_error;
        if (!read_session_id(
                request.params, session_id, session_error))
        {
            return ControlMethodResult::error(
                "invalid_session", std::move(session_error));
        }
        ServerSession* session
            = ensure_session(session_id, session_error);
        if (!session || !session->agent_service)
        {
            return ControlMethodResult::error(
                "session_unavailable",
                session_error.empty()
                    ? "Server Session agents are unavailable."
                    : std::move(session_error));
        }
        if (request.method == "agent.restart"
            || request.method == "agent.send_text"
            || request.method == "agent.send_keys")
        {
            if (!request.params.is_object()
                || !request.params.contains("instance_id")
                || !request.params["instance_id"].is_string())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    request.method
                        + " requires 'instance_id'.");
            }
            const std::string instance_id
                = request.params["instance_id"]
                      .get<std::string>();
            const auto& agents
                = session->agent_service->snapshot().agents;
            const auto agent = std::ranges::find_if(
                agents,
                [&instance_id](
                    const ServerAgentProjection& value) {
                    return value.identity.instance_id
                        == instance_id;
                });
            if (agent == agents.end())
            {
                return ControlMethodResult::error(
                    "not_found", "Agent not found.");
            }
            const std::string terminal_id
                = agent->terminal_id;
            const auto terminal
                = session->terminals.find(terminal_id);
            if (terminal == session->terminals.end())
            {
                return ControlMethodResult::error(
                    "agent_replaced",
                    "The agent terminal no longer exists.");
            }
            if (request.method == "agent.restart")
            {
                std::string restart_error;
                if (!terminal->second.service
                         ->restart_runtime(restart_error))
                {
                    return ControlMethodResult::error(
                        "restart_failed",
                        std::move(restart_error));
                }
                refresh_agents(*session,
                    std::chrono::steady_clock::now());
                return ControlMethodResult::success({
                    { "accepted", true },
                    { "terminal_id", terminal_id },
                    { "runtime_generation",
                        terminal->second.service
                            ->generation() },
                });
            }

            std::string bytes;
            if (request.method == "agent.send_text")
            {
                if (!request.params.contains("text")
                    || !request.params["text"].is_string())
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "agent.send_text requires string 'text'.");
                }
                bytes = request.params["text"].get<std::string>();
                if (bytes.size() > 64 * 1024)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "Agent text exceeds 64 KiB.");
                }
            }
            else
            {
                if (!request.params.contains("keys")
                    || !request.params["keys"].is_array()
                    || request.params["keys"].size() > 64)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "agent.send_keys requires at most 64 keys.");
                }
                std::vector<std::string> keys;
                keys.reserve(
                    request.params["keys"].size());
                for (const auto& value
                    : request.params["keys"])
                {
                    if (!value.is_string())
                    {
                        return ControlMethodResult::error(
                            "invalid_params",
                            "Every key must be a string.");
                    }
                    keys.push_back(value.get<std::string>());
                }
                std::string key_error;
                auto encoded
                    = encode_agent_keys(keys, key_error);
                if (!encoded)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        std::move(key_error));
                }
                bytes = std::move(*encoded);
            }
            if (!terminal->second.runtime->send_input(bytes))
            {
                return ControlMethodResult::error(
                    "input_failed",
                    "The agent terminal rejected input.");
            }
            return session->agent_service->handle(
                "agent.get",
                { { "instance_id", instance_id } });
        }
        return session->agent_service->handle(
            request.method, request.params);
    }
    if (request.method == "pane.report_agent_session")
    {
        std::string session_id;
        std::string session_error;
        if (!read_session_id(
                request.params, session_id, session_error))
        {
            return ControlMethodResult::error(
                "invalid_session", std::move(session_error));
        }
        ServerSession* session
            = ensure_session(session_id, session_error);
        if (!session || !session->topology_service
            || !session->agent_service)
        {
            return ControlMethodResult::error(
                "session_unavailable",
                session_error.empty()
                    ? "Server Session is unavailable."
                    : std::move(session_error));
        }
        const auto required_string
            = [&](const char* name)
            -> std::optional<std::string> {
            if (!request.params.is_object()
                || !request.params.contains(name)
                || !request.params[name].is_string()
                || request.params[name]
                       .get_ref<const std::string&>()
                       .empty())
            {
                return std::nullopt;
            }
            return request.params[name].get<std::string>();
        };
        const auto read_unsigned
            = [&](const char* name)
            -> std::optional<uint64_t> {
            if (!request.params.contains(name))
                return std::nullopt;
            const auto& value = request.params[name];
            if (value.is_number_unsigned())
                return value.get<uint64_t>();
            if (value.is_number_integer()
                && value.get<int64_t>() >= 0)
            {
                return static_cast<uint64_t>(
                    value.get<int64_t>());
            }
            return std::nullopt;
        };
        const auto server_epoch
            = required_string("server_epoch");
        const auto pane_id = required_string("pane_id");
        const auto instance_id
            = required_string("agent_instance_id");
        const auto source = required_string("source");
        const auto agent_kind = required_string("agent");
        const auto ref_kind_text
            = required_string("ref_kind");
        const auto ref_value = required_string("ref_value");
        const auto integration_version
            = read_unsigned("integration_version");
        const auto sequence = read_unsigned("sequence");
        const auto runtime_generation
            = read_unsigned("runtime_generation");
        if (!server_epoch || !pane_id || !instance_id
            || !source || !agent_kind || !ref_kind_text
            || !ref_value || !integration_version
            || *integration_version == 0
            || *integration_version > UINT32_MAX
            || !sequence || *sequence == 0
            || !runtime_generation
            || *runtime_generation == 0)
        {
            return ControlMethodResult::error(
                "invalid_params",
                "pane.report_agent_session requires server epoch, "
                "runtime generation, complete routing, source, "
                "version, sequence, and reference fields.");
        }
        if (*server_epoch != epoch_value)
        {
            return ControlMethodResult::error(
                "server_replaced",
                "The session report targets an old server epoch.");
        }
        const auto ref_kind
            = parse_agent_session_ref_kind(*ref_kind_text);
        if (!ref_kind)
        {
            return ControlMethodResult::error(
                "invalid_params",
                "Unknown native session reference kind.");
        }

        refresh_agents(
            *session, std::chrono::steady_clock::now());
        const auto& agents
            = session->agent_service->snapshot().agents;
        const auto agent = std::ranges::find_if(
            agents,
            [&](const ServerAgentProjection& value) {
                return value.identity.instance_id
                    == *instance_id;
            });
        if (agent == agents.end()
            || agent->pane_id != *pane_id
            || agent->identity.kind != *agent_kind)
        {
            return ControlMethodResult::error(
                "routing_mismatch",
                "Agent routing identity does not match the pane.");
        }
        if (agent->generation.value
            != *runtime_generation)
        {
            return ControlMethodResult::error(
                "agent_replaced",
                "The session report targets an old agent runtime generation.");
        }

        AgentSessionRef session_ref{
            .source = *source,
            .agent_kind = *agent_kind,
            .integration_version
            = static_cast<uint32_t>(*integration_version),
            .sequence = *sequence,
            .kind = *ref_kind,
            .value = *ref_value,
        };
        auto reported
            = session->topology_service
                  ->report_agent_session(
                      *pane_id, *instance_id,
                      session_ref);
        if (!reported.ok)
            return reported;
        refresh_agents(
            *session, std::chrono::steady_clock::now());
        return session->agent_service->handle(
            "agent.get",
            { { "instance_id", *instance_id } });
    }
    if (request.method == "pane.read")
    {
        std::string session_id;
        std::string session_error;
        if (!read_session_id(
                request.params, session_id, session_error))
        {
            return ControlMethodResult::error(
                "invalid_session", std::move(session_error));
        }
        ServerSession* session
            = ensure_session(session_id, session_error);
        if (!session || !session->topology_service)
        {
            return ControlMethodResult::error(
                "session_unavailable",
                session_error.empty()
                    ? "Server Session is unavailable."
                    : std::move(session_error));
        }
        if (!request.params.contains("pane_id")
            || !request.params["pane_id"].is_string())
        {
            return ControlMethodResult::error(
                "invalid_params",
                "pane.read requires a non-empty 'pane_id'.");
        }
        int max_lines = 50;
        if (request.params.contains("lines"))
        {
            if (!request.params["lines"].is_number_integer())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'lines' must be between 1 and 200.");
            }
            max_lines = request.params["lines"].get<int>();
        }
        if (max_lines < 1 || max_lines > 200)
        {
            return ControlMethodResult::error(
                "invalid_params",
                "'lines' must be between 1 and 200.");
        }
        const std::string pane_id
            = request.params["pane_id"].get<std::string>();
        for (const auto& [terminal_id, endpoint]
            : session->terminals)
        {
            const TopologySnapshot& topology
                = session->topology_service->snapshot();
            for (const auto& space : topology.spaces)
            {
                for (const auto& tab : space.tabs)
                {
                    const auto pane = std::ranges::find_if(
                        tab.panes,
                        [&](const TopologyPane& value) {
                            return value.pane_id == pane_id
                                && value.terminal_id
                                    == terminal_id;
                        });
                    if (pane == tab.panes.end())
                        continue;
                    const auto observation
                        = endpoint.runtime
                              ->capture_agent_observation(
                                  max_lines, 64 * 1024);
                    if (!observation)
                    {
                        return ControlMethodResult::error(
                            "unsupported",
                            "Pane does not expose readable terminal text.");
                    }
                    return ControlMethodResult::success({
                        { "pane_id", pane_id },
                        { "space_id", space.space_id },
                        { "tab_id", tab.tab_id },
                        { "lines",
                            observation->bottom_rows },
                        { "output_generation",
                            observation
                                ->output_generation },
                    });
                }
            }
        }
        return ControlMethodResult::error(
            "not_found", "Pane not found.");
    }
    if (request.method == "server.shutdown")
    {
        request_stop();
        return ControlMethodResult::success({
            { "stopping", true },
            { "server_pid", pid },
            { "server_epoch", epoch_value },
        });
    }
    return ControlMethodResult::error(
        "unknown_method", "Unknown Draxul server method.");
}

bool ServerKernel::Impl::read_fake_client_id(
    const nlohmann::json& params, std::string& client_id) const
{
    if (!params.is_object()
        || !params.contains("client_id")
        || !params["client_id"].is_string())
    {
        return false;
    }
    client_id = params["client_id"].get<std::string>();
    return !client_id.empty() && client_id.size() <= 128;
}

bool ServerKernel::Impl::read_fake_version(
    const nlohmann::json& params, RemoteTerminalVersion& version) const
{
    if (!params.is_object()
        || !params.contains("server_epoch")
        || !params["server_epoch"].is_string()
        || !params.contains("terminal_id")
        || !params["terminal_id"].is_string()
        || !params.contains("generation")
        || !params["generation"].is_number_unsigned()
        || !params.contains("after_sequence")
        || !params["after_sequence"].is_number_unsigned())
    {
        return false;
    }
    version.server_epoch = params["server_epoch"].get<std::string>();
    version.terminal_id = params["terminal_id"].get<std::string>();
    version.generation = params["generation"].get<uint64_t>();
    version.sequence = params["after_sequence"].get<uint64_t>();
    return true;
}

RemoteTerminalVersion ServerKernel::Impl::fake_version() const
{
    return {
        .server_epoch = epoch_value,
        .terminal_id = std::string(kFakeRemoteTerminalId),
        .generation = fake_generation,
        .sequence = fake_sequence,
    };
}

RemoteTerminalEvent ServerKernel::Impl::fake_snapshot_event() const
{
    return {
        .kind = RemoteTerminalEventKind::Snapshot,
        .version = fake_version(),
        .controller_client_id = fake_controller_client_id,
        .snapshot = fake_terminal->snapshot(),
    };
}

RemoteTerminalEvent ServerKernel::Impl::make_fake_delta_event()
{
    ++fake_sequence;
    return {
        .kind = RemoteTerminalEventKind::Delta,
        .version = fake_version(),
        .controller_client_id = fake_controller_client_id,
        .delta = fake_terminal->take_delta(),
    };
}

RemoteTerminalEvent ServerKernel::Impl::make_fake_controller_event()
{
    ++fake_sequence;
    return {
        .kind = RemoteTerminalEventKind::Controller,
        .version = fake_version(),
        .controller_client_id = fake_controller_client_id,
    };
}

void ServerKernel::Impl::broadcast_fake_event(
    const RemoteTerminalEvent& event)
{
    for (auto& [client_id, subscriber] : fake_subscribers)
    {
        (void)client_id;
        if (subscriber.needs_resync)
            continue;
        if (subscriber.events.size() >= kRemoteTerminalQueueLimit)
        {
            subscriber.events.clear();
            subscriber.needs_resync = true;
            continue;
        }
        subscriber.events.push_back(event);
    }
}

ControlMethodResult ServerKernel::Impl::attach_fake_terminal(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_fake_client_id(params, client_id))
    {
        return ControlMethodResult::error(
            "invalid_client", "A valid client_id is required.");
    }

    fake_subscribers[client_id] = {};
    if (fake_controller_client_id.empty())
    {
        fake_controller_client_id = client_id;
        broadcast_fake_event(make_fake_controller_event());
        fake_subscribers[client_id].events.clear();
    }
    RemoteTerminalAttach attach{
        .pane = {
            .pane_id = std::string(kFakeRemotePaneId),
            .terminal_id = std::string(kFakeRemoteTerminalId),
            .name = "Fake Remote",
            .execution_domain = "server_terminal",
        },
        .state = fake_snapshot_event(),
    };
    return ControlMethodResult::success(
        remote_terminal_attach_to_json(attach));
}

ControlMethodResult ServerKernel::Impl::poll_fake_terminal(
    const nlohmann::json& params)
{
    std::string client_id;
    RemoteTerminalVersion requested;
    if (!read_fake_client_id(params, client_id)
        || !read_fake_version(params, requested))
    {
        return ControlMethodResult::error(
            "invalid_poll", "A valid client and terminal version are required.");
    }
    auto subscriber = fake_subscribers.find(client_id);
    if (subscriber == fake_subscribers.end())
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the fake terminal before polling.");
    }
    if (requested.server_epoch != epoch_value)
    {
        return ControlMethodResult::error(
            "stale_epoch", "The terminal server epoch has changed.");
    }
    if (requested.terminal_id != kFakeRemoteTerminalId)
    {
        return ControlMethodResult::error(
            "stale_terminal", "The terminal identity has changed.");
    }
    if (requested.generation != fake_generation)
    {
        return ControlMethodResult::error(
            "stale_generation", "The terminal generation has changed.");
    }
    if (requested.sequence > fake_sequence)
    {
        return ControlMethodResult::error(
            "stale_sequence", "The client sequence is ahead of the server.");
    }

    auto& delivery = subscriber->second;
    while (!delivery.events.empty()
        && delivery.events.front().version.sequence <= requested.sequence)
    {
        delivery.events.pop_front();
    }

    nlohmann::json events = nlohmann::json::array();
    if (delivery.needs_resync)
    {
        events.push_back(remote_terminal_event_to_json(fake_snapshot_event()));
        delivery.events.clear();
        delivery.needs_resync = false;
    }
    else
    {
        size_t count = 0;
        size_t payload_bytes = 0;
        for (const auto& event : delivery.events)
        {
            if (count++ >= kRemoteTerminalMaxEventsPerPoll)
                break;
            auto encoded = remote_terminal_event_to_json(event);
            const size_t encoded_bytes = encoded.dump().size();
            if (!events.empty()
                && payload_bytes + encoded_bytes
                    > kRemoteTerminalPollPayloadBudget)
            {
                break;
            }
            events.push_back(std::move(encoded));
            payload_bytes += encoded_bytes;
        }
    }
    return ControlMethodResult::success({
        { "events", std::move(events) },
        { "server_sequence", fake_sequence },
    });
}

ControlMethodResult ServerKernel::Impl::input_fake_terminal(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_fake_client_id(params, client_id)
        || !params.contains("text") || !params["text"].is_string())
    {
        return ControlMethodResult::error(
            "invalid_input", "A valid client_id and text are required.");
    }
    if (!fake_subscribers.contains(client_id))
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the fake terminal before sending input.");
    }
    if (client_id != fake_controller_client_id)
    {
        return ControlMethodResult::error(
            "not_controller", "This client is observing the terminal.");
    }
    const std::string text = params["text"].get<std::string>();
    if (text.empty() || text.size() > 64 * 1024)
    {
        return ControlMethodResult::error(
            "invalid_input", "Terminal input must be between 1 and 65536 bytes.");
    }
    fake_terminal->echo_input(text);
    const auto event = make_fake_delta_event();
    broadcast_fake_event(event);
    return ControlMethodResult::success({
        { "accepted", true },
        { "sequence", event.version.sequence },
    });
}

ControlMethodResult ServerKernel::Impl::resize_fake_terminal(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_fake_client_id(params, client_id)
        || !params.contains("cols") || !params["cols"].is_number_integer()
        || !params.contains("rows") || !params["rows"].is_number_integer())
    {
        return ControlMethodResult::error(
            "invalid_resize", "A valid client_id, cols, and rows are required.");
    }
    if (!fake_subscribers.contains(client_id))
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the fake terminal before resizing.");
    }
    if (client_id != fake_controller_client_id)
    {
        return ControlMethodResult::error(
            "not_controller", "This client is observing the terminal.");
    }
    const int cols = params["cols"].get<int>();
    const int rows = params["rows"].get<int>();
    if (cols <= 0 || rows <= 0
        || cols > TerminalStateLimits::kMaxColumns
        || rows > TerminalStateLimits::kMaxRows
        || static_cast<size_t>(cols) * static_cast<size_t>(rows)
            > TerminalStateLimits::kMaxCells)
    {
        return ControlMethodResult::error(
            "invalid_resize", "Terminal dimensions are out of range.");
    }
    fake_terminal->resize(cols, rows);
    const auto event = make_fake_delta_event();
    broadcast_fake_event(event);
    return ControlMethodResult::success({
        { "accepted", true },
        { "sequence", event.version.sequence },
    });
}

ControlMethodResult ServerKernel::Impl::take_fake_terminal_control(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_fake_client_id(params, client_id))
    {
        return ControlMethodResult::error(
            "invalid_client", "A valid client_id is required.");
    }
    if (!fake_subscribers.contains(client_id))
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the fake terminal before taking control.");
    }
    if (fake_controller_client_id != client_id)
    {
        fake_controller_client_id = client_id;
        const auto event = make_fake_controller_event();
        broadcast_fake_event(event);
    }
    return ControlMethodResult::success({
        { "controller_client_id", fake_controller_client_id },
        { "sequence", fake_sequence },
    });
}

ControlMethodResult ServerKernel::Impl::disconnect_fake_terminal(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_fake_client_id(params, client_id))
    {
        return ControlMethodResult::error(
            "invalid_client", "A valid client_id is required.");
    }
    const bool was_controller = fake_controller_client_id == client_id;
    fake_subscribers.erase(client_id);
    if (was_controller)
    {
        fake_controller_client_id.clear();
        broadcast_fake_event(make_fake_controller_event());
    }
    return ControlMethodResult::success({ { "disconnected", true } });
}

ServerStatusSnapshot ServerKernel::Impl::status_snapshot() const
{
    size_t connected_clients = 0;
    {
        std::lock_guard guard(mutex);
        connected_clients = clients.size();
    }
    const auto now = std::chrono::steady_clock::now();
    const uint64_t uptime_ms = started
        ? static_cast<uint64_t>(
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - started_at)
                  .count())
        : 0;
    std::vector<ServerSessionStatusSnapshot> session_statuses;
    session_statuses.reserve(sessions.size());
    size_t space_count = 0;
    size_t terminal_count = 0;
    for (const auto& [session_id, session] : sessions)
    {
        const size_t spaces = session->topology_service
            ? session->topology_service->snapshot().spaces.size()
            : (session->restored_topology
                      ? session->restored_topology->spaces.size()
                      : 0);
        const size_t live_terminals
            = static_cast<size_t>(std::ranges::count_if(
                session->terminals,
                [](const auto& item) {
                    return item.second.service->started();
                }));
        space_count += spaces;
        terminal_count += session->terminals.size();
        session_statuses.push_back({
            .session_id = session_id,
            .spaces = spaces,
            .terminals = session->terminals.size(),
            .live_terminals = live_terminals,
            .checkpoint_path = session->persistence_path.string(),
            .checkpoint_state = session->checkpoint_state,
            .last_checkpoint_unix_ms
            = session->last_checkpoint_unix_ms,
            .checkpoint_error = session->checkpoint_error,
            .restore_warnings = session->restore_warnings,
        });
    }
    std::ranges::sort(session_statuses,
        {}, &ServerSessionStatusSnapshot::session_id);

    std::string checkpoint_path;
    std::string checkpoint_state;
    uint64_t last_checkpoint_unix_ms = 0;
    std::string checkpoint_error;
    std::vector<std::string> restore_warnings
        = unassigned_restore_warnings;
    if (const auto default_session = sessions.find("default");
        default_session != sessions.end())
    {
        const ServerSession& session = *default_session->second;
        checkpoint_path = session.persistence_path.string();
        checkpoint_state = session.checkpoint_state;
        last_checkpoint_unix_ms = session.last_checkpoint_unix_ms;
        checkpoint_error = session.checkpoint_error;
        restore_warnings.insert(restore_warnings.end(),
            session.restore_warnings.begin(),
            session.restore_warnings.end());
    }

    return {
        .state = stop_requested ? "stopping" : (started ? "ready" : "stopped"),
        .protocol_major = options.protocol_major,
        .protocol_minor = options.protocol_minor,
        .server_pid = pid,
        .server_epoch = epoch_value,
        .build_version = options.build_version,
        .uptime_ms = uptime_ms,
        .connected_clients = connected_clients,
        .sessions = sessions.size(),
        .spaces = space_count,
        .terminals = terminal_count,
        .checkpoint_path = std::move(checkpoint_path),
        .checkpoint_state = std::move(checkpoint_state),
        .last_checkpoint_unix_ms
        = last_checkpoint_unix_ms,
        .checkpoint_error = std::move(checkpoint_error),
        .restore_warnings = std::move(restore_warnings),
        .session_statuses = std::move(session_statuses),
    };
}

std::optional<std::string>
ServerKernel::Impl::create_server_terminal(
    std::string_view session_id, std::string_view pane_id,
    std::string_view name,
    std::string& error)
{
    const auto found = sessions.find(std::string(session_id));
    if (found == sessions.end())
    {
        error = "The requested server Session does not exist.";
        return std::nullopt;
    }
    ServerSession& session = *found->second;
    const std::string terminal_id
        = "terminal-"
        + std::to_string(session.next_terminal_serial++);
    if (!create_server_terminal_with_id(
            session_id, terminal_id, pane_id, name, error))
    {
        return std::nullopt;
    }
    return terminal_id;
}

void ServerKernel::Impl::destroy_server_terminal(
    std::string_view session_id, std::string_view terminal_id)
{
    const auto session = sessions.find(std::string(session_id));
    if (session != sessions.end())
        session->second->terminals.erase(std::string(terminal_id));
}

bool ServerKernel::Impl::restart_server_terminal(
    std::string_view session_id, std::string_view terminal_id,
    std::string& error)
{
    const auto session = sessions.find(std::string(session_id));
    if (session == sessions.end())
    {
        error = "The requested server Session does not exist.";
        return false;
    }
    const auto terminal
        = session->second->terminals.find(std::string(terminal_id));
    if (terminal == session->second->terminals.end())
    {
        error = "The requested server terminal does not exist.";
        return false;
    }
    return terminal->second.service->restart_runtime(error);
}

void ServerKernel::Impl::refresh_agents(
    ServerSession& session,
    std::chrono::steady_clock::time_point now)
{
    if (!session.topology_service || !session.agent_service)
        return;
    std::vector<ServerAgentRuntimeView> runtimes;
    const TopologySnapshot& topology
        = session.topology_service->snapshot();
    for (const auto& space : topology.spaces)
    {
        for (const auto& tab : space.tabs)
        {
            for (const auto& pane : tab.panes)
            {
                if (pane.domain
                    != TopologyPaneDomain::ServerTerminal)
                {
                    continue;
                }
                const auto terminal
                    = session.terminals.find(pane.terminal_id);
                if (terminal == session.terminals.end())
                    continue;
                const auto& endpoint = terminal->second;
                const bool running
                    = endpoint.runtime->is_running();
                runtimes.push_back({
                    .space_id = space.space_id,
                    .tab_id = tab.tab_id,
                    .pane_id = pane.pane_id,
                    .terminal_id = pane.terminal_id,
                    .declared_identity = pane.agent,
                    .session_ref = pane.agent_session,
                    .generation = {
                        endpoint.service->generation(),
                    },
                    .runtime_running = running,
                    .exit_code
                    = endpoint.runtime->exit_code(),
                    .process_observation = running
                        ? endpoint.runtime
                              ->capture_agent_process_observation()
                        : std::nullopt,
                    .terminal_observation = running
                        ? endpoint.runtime
                              ->capture_agent_observation(
                                  12, 8 * 1024)
                        : std::nullopt,
                });
            }
        }
    }
    session.agent_service->update(runtimes, now);
}

int ServerKernel::Impl::run_until_stopped()
{
    if (!started)
        return 1;
    std::string initialization_error;
    if (!initialize_services(initialization_error))
    {
        DRAXUL_LOG_ERROR(LogCategory::App,
            "Draxul server could not initialize restored Session services: %s",
            initialization_error.c_str());
        stop();
        return 1;
    }
    while (!stop_requested)
    {
        for (auto& [session_id, session] : sessions)
        {
            (void)session_id;
            for (auto& [terminal_id, endpoint]
                : session->terminals)
            {
                (void)terminal_id;
                endpoint.service->pump();
            }
        }
        if (const uint32_t listener_error = control.take_listener_error();
            listener_error != 0)
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Draxul server control listener recreation failed with Windows error %u; retrying",
                listener_error);
        }
        control.process_pending(
            [this](const ControlRequest& request) {
                return handle_request(request);
            });
        const auto now = std::chrono::steady_clock::now();
        for (auto& [session_id, session] : sessions)
        {
            (void)session_id;
            if (now >= session->next_agent_refresh_at)
            {
                refresh_agents(*session, now);
                session->next_agent_refresh_at
                    = now + std::chrono::milliseconds(500);
            }
        }
        if (options.session_checkpoint_interval.count() > 0)
        {
            for (auto& [session_id, session] : sessions)
            {
                if (!session->checkpoint_enabled
                    || now < session->next_checkpoint_at)
                {
                    continue;
                }
                if (session->topology_service
                    && (!session->checkpoint_file_present
                        || session->topology_service
                                ->snapshot()
                                .revision
                            != session->last_checkpoint_revision))
                {
                    std::string periodic_error;
                    if (!checkpoint_session(
                            session_id, periodic_error))
                    {
                        DRAXUL_LOG_WARN(LogCategory::App,
                            "Draxul server periodic Session '%s' checkpoint failed: %s",
                            session_id.c_str(),
                            periodic_error.c_str());
                    }
                }
                session->next_checkpoint_at
                    = now + options.session_checkpoint_interval;
            }
        }
        std::unique_lock lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(25),
            [this] { return stop_requested.load(); });
    }
    control.process_pending(
        [this](const ControlRequest& request) {
            return handle_request(request);
        });
    for (const auto& [session_id, session] : sessions)
    {
        (void)session;
        std::string checkpoint_error;
        if (!checkpoint_session(session_id, checkpoint_error))
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Draxul server Session '%s' checkpoint was not written: %s",
                session_id.c_str(),
                checkpoint_error.c_str());
        }
    }
    reset_services();
    stop();
    return 0;
}

void ServerKernel::Impl::request_stop()
{
    stop_requested = true;
    wake.notify_all();
}

void ServerKernel::Impl::stop()
{
    request_stop();
    control.stop();
    remove_starting_marker();
    started = false;
}

ServerKernel::ServerKernel(ServerKernelOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{
}

ServerKernel::~ServerKernel()
{
    stop();
}

ServerStartResult ServerKernel::start()
{
    return impl_->start();
}

int ServerKernel::run_until_stopped()
{
    return impl_->run_until_stopped();
}

void ServerKernel::request_stop()
{
    impl_->request_stop();
}

void ServerKernel::stop()
{
    impl_->stop();
}

bool ServerKernel::running() const
{
    return impl_->started;
}

const std::string& ServerKernel::epoch() const
{
    return impl_->epoch_value;
}

uint64_t ServerKernel::process_id() const
{
    return impl_->pid;
}

ServerStatusSnapshot ServerKernel::status_snapshot() const
{
    return impl_->status_snapshot();
}

} // namespace draxul
