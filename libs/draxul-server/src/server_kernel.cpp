#include <draxul/server_kernel.h>

#include "fake_terminal_runtime.h"
#include "remote_terminal_service.h"
#include "server_terminal_runtime.h"
#include "session_topology_bridge.h"
#include "topology_service.h"

#include <draxul/control_plane.h>
#include <draxul/log.h>
#include <draxul/remote_terminal_protocol.h>
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

const std::vector<std::string>& server_capabilities()
{
    static const std::vector<std::string> capabilities{
        "client-registration",
        "controller-lease",
        "fake-remote-terminal",
        "graceful-shutdown",
        "multi-terminal-v1",
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

class ServerKernel::Impl
{
public:
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
    void reset_services();
    bool checkpoint_session(std::string& error);
    std::optional<std::string> create_server_terminal(
        std::string_view pane_id, std::string_view name,
        std::string& error);
    bool create_server_terminal_with_id(std::string terminal_id,
        std::string_view pane_id, std::string_view name,
        std::string& error);
    void destroy_server_terminal(std::string_view terminal_id);
    bool restart_server_terminal(
        std::string_view terminal_id, std::string& error);

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
    std::unordered_map<std::string, ServerTerminalEndpoint>
        server_terminals;
    uint64_t next_terminal_serial = 2;
    std::unique_ptr<TopologyService> topology_service;
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
    std::string terminal_id, std::string_view pane_id,
    std::string_view name, std::string& error)
{
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
    if (!server_terminals.emplace(
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
    next_terminal_serial = std::max(
        next_terminal_serial, numeric_suffix(terminal_id) + 1);
    return true;
}

void ServerKernel::Impl::reset_services()
{
    topology_service.reset();
    server_terminals.clear();
    fake_terminal.reset();
    next_terminal_serial = 2;
}

bool ServerKernel::Impl::prepare_session_restore(std::string&)
{
    persistence_path = options.session_state_file.empty()
        ? server_session_state_path(options.runtime_directory)
        : options.session_state_file;
    checkpoint_enabled = true;
    checkpoint_file_present
        = std::filesystem::exists(persistence_path);
    checkpoint_state = checkpoint_file_present
        ? "restoring"
        : "pending";
    checkpoint_error.clear();
    last_checkpoint_unix_ms = 0;
    last_checkpoint_revision = 0;
    restore_warnings.clear();
    restored_topology.reset();

    if (checkpoint_file_present)
    {
        std::string load_error;
        auto session = load_session_state_from_path(
            persistence_path, &load_error);
        std::optional<SessionTopologyRestore> restored;
        if (session)
            restored = restore_session_topology(*session, load_error);
        if (!restored)
        {
            checkpoint_enabled = false;
            checkpoint_state = "disabled";
            checkpoint_error = load_error.empty()
                ? "Saved Session could not be restored."
                : load_error;
            restore_warnings.push_back(
                checkpoint_error);
            DRAXUL_LOG_WARN(LogCategory::App,
                "Draxul server retained unreadable Session checkpoint %s: %s",
                persistence_path.string().c_str(),
                restore_warnings.back().c_str());
        }
        else
        {
            restore_warnings = std::move(restored->warnings);
            if (!restore_warnings.empty())
            {
                checkpoint_enabled = false;
                checkpoint_state = "disabled";
                checkpoint_error
                    = "Checkpoint disabled after partial restore.";
            }
            else
            {
                checkpoint_state = "restored";
            }
            restored_topology = std::move(restored->topology);
        }
    }
    return true;
}

bool ServerKernel::Impl::initialize_services(std::string& error)
{
    reset_services();
    fake_terminal = std::make_unique<FakeTerminalRuntime>();

    TopologyServiceCallbacks callbacks{
        .create_server_terminal
        = [this](std::string_view pane_id,
              std::string_view name, std::string& callback_error) { return create_server_terminal(
                                                                        pane_id, name, callback_error); },
        .destroy_server_terminal
        = [this](std::string_view terminal_id) { destroy_server_terminal(terminal_id); },
        .restart_server_terminal
        = [this](std::string_view terminal_id,
              std::string& callback_error) { return restart_server_terminal(
                                                 terminal_id, callback_error); },
    };

    if (restored_topology)
    {
        for (const auto& space : restored_topology->spaces)
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
                            pane.terminal_id, pane.pane_id,
                            pane.name, error))
                    {
                        reset_services();
                        return false;
                    }
                }
            }
        }
        topology_service = std::make_unique<TopologyService>(
            *restored_topology, std::move(callbacks));
        last_checkpoint_revision
            = topology_service->snapshot().revision;
        next_checkpoint_at = std::chrono::steady_clock::now()
            + options.session_checkpoint_interval;
        return true;
    }

    if (!create_server_terminal_with_id(
            std::string(kServerShellTerminalId),
            kServerShellPaneId, "Server Shell", error))
    {
        reset_services();
        return false;
    }
    topology_service = std::make_unique<TopologyService>(
        "default", std::move(callbacks));
    next_checkpoint_at = std::chrono::steady_clock::now()
        + options.session_checkpoint_interval;
    return true;
}

bool ServerKernel::Impl::checkpoint_session(std::string& error)
{
    auto fail = [&](std::string message, std::string state = "failed") {
        checkpoint_state = std::move(state);
        checkpoint_error = message.size() > 4096
            ? message.substr(0, 4096)
            : message;
        error = checkpoint_error;
        return false;
    };
    if (!topology_service)
        return fail("Server topology is unavailable.");
    if (!checkpoint_enabled)
        return fail(
            "Checkpoint disabled to preserve the previous Session file after restore warnings.",
            "disabled");
    auto session = capture_session_topology(
        topology_service->snapshot(), error);
    if (!session)
        return fail(error);
    if (!save_session_state_to_path(
            *session, persistence_path, &error))
    {
        return fail(error.empty()
                ? "Unable to save Session checkpoint."
                : error);
    }
    checkpoint_state = "ok";
    checkpoint_error.clear();
    last_checkpoint_unix_ms = current_unix_time_ms();
    last_checkpoint_revision
        = topology_service->snapshot().revision;
    checkpoint_file_present = true;
    error.clear();
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
        std::string terminal_id
            = std::string(kServerShellTerminalId);
        if (request.params.is_object()
            && request.params.contains("terminal_id")
            && request.params["terminal_id"].is_string())
        {
            terminal_id
                = request.params["terminal_id"].get<std::string>();
        }
        const auto terminal
            = server_terminals.find(terminal_id);
        if (terminal == server_terminals.end())
        {
            return ControlMethodResult::error(
                "terminal_not_found",
                "The requested server terminal does not exist.");
        }
        return terminal->second.service->handle(
            request.method, request.params);
    }
    if (topology_service && topology_service->handles(request.method))
        return topology_service->handle(request.method, request.params);
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
    return {
        .state = stop_requested ? "stopping" : (started ? "ready" : "stopped"),
        .protocol_major = options.protocol_major,
        .protocol_minor = options.protocol_minor,
        .server_pid = pid,
        .server_epoch = epoch_value,
        .build_version = options.build_version,
        .uptime_ms = uptime_ms,
        .connected_clients = connected_clients,
        .sessions = topology_service ? 1u : 0u,
        .spaces = topology_service
            ? topology_service->snapshot().spaces.size()
            : 0u,
        .terminals = 1
            + static_cast<size_t>(std::ranges::count_if(
                server_terminals,
                [](const auto& item) {
                    return item.second.service->started();
                })),
        .checkpoint_path = persistence_path.string(),
        .checkpoint_state = checkpoint_state,
        .last_checkpoint_unix_ms
        = last_checkpoint_unix_ms,
        .checkpoint_error = checkpoint_error,
        .restore_warnings = restore_warnings,
    };
}

std::optional<std::string>
ServerKernel::Impl::create_server_terminal(
    std::string_view pane_id, std::string_view name,
    std::string& error)
{
    const std::string terminal_id
        = "terminal-" + std::to_string(next_terminal_serial++);
    if (!create_server_terminal_with_id(
            terminal_id, pane_id, name, error))
    {
        return std::nullopt;
    }
    return terminal_id;
}

void ServerKernel::Impl::destroy_server_terminal(
    std::string_view terminal_id)
{
    server_terminals.erase(std::string(terminal_id));
}

bool ServerKernel::Impl::restart_server_terminal(
    std::string_view terminal_id, std::string& error)
{
    const auto terminal
        = server_terminals.find(std::string(terminal_id));
    if (terminal == server_terminals.end())
    {
        error = "The requested server terminal does not exist.";
        return false;
    }
    return terminal->second.service->restart_runtime(error);
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
        for (auto& [terminal_id, endpoint] : server_terminals)
        {
            (void)terminal_id;
            endpoint.service->pump();
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
        if (checkpoint_enabled
            && options.session_checkpoint_interval.count() > 0
            && now >= next_checkpoint_at)
        {
            if (topology_service
                && (!checkpoint_file_present
                    || topology_service->snapshot().revision
                        != last_checkpoint_revision))
            {
                std::string periodic_error;
                if (!checkpoint_session(periodic_error))
                {
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "Draxul server periodic Session checkpoint failed: %s",
                        periodic_error.c_str());
                }
            }
            next_checkpoint_at
                = now + options.session_checkpoint_interval;
        }
        std::unique_lock lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(25),
            [this] { return stop_requested.load(); });
    }
    control.process_pending(
        [this](const ControlRequest& request) {
            return handle_request(request);
        });
    std::string checkpoint_error;
    if (!checkpoint_session(checkpoint_error))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Draxul server Session checkpoint was not written: %s",
            checkpoint_error.c_str());
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
