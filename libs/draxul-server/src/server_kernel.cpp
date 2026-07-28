#include <draxul/server_kernel.h>

#include "fake_terminal_runtime.h"

#include <draxul/control_plane.h>
#include <draxul/log.h>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_protocol.h>

#include <algorithm>
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

uint64_t current_process_id()
{
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(::getpid());
#endif
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

const std::vector<std::string>& server_capabilities()
{
    static const std::vector<std::string> capabilities{
        "client-registration",
        "controller-lease",
        "fake-remote-terminal",
        "graceful-shutdown",
        "ordered-terminal-events",
        "status",
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

    FakeTerminalRuntime fake_terminal;
    uint64_t fake_generation = 1;
    uint64_t fake_sequence = 0;
    std::string fake_controller_client_id;
    std::unordered_map<std::string, FakeSubscriber> fake_subscribers;
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

ServerStartResult ServerKernel::Impl::start()
{
    if (started)
        return { ServerStartDisposition::Started, {} };
    if (options.runtime_directory.empty())
        return { ServerStartDisposition::Failed, "Server runtime directory is empty." };

    stop_requested = false;
    publish_starting_marker();
    std::string error;
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
        .snapshot = fake_terminal.snapshot(),
    };
}

RemoteTerminalEvent ServerKernel::Impl::make_fake_delta_event()
{
    ++fake_sequence;
    return {
        .kind = RemoteTerminalEventKind::Delta,
        .version = fake_version(),
        .controller_client_id = fake_controller_client_id,
        .delta = fake_terminal.take_delta(),
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
        for (const auto& event : delivery.events)
        {
            if (count++ >= kRemoteTerminalMaxEventsPerPoll)
                break;
            events.push_back(remote_terminal_event_to_json(event));
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
    fake_terminal.echo_input(text);
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
    fake_terminal.resize(cols, rows);
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
        .terminals = 1,
    };
}

int ServerKernel::Impl::run_until_stopped()
{
    if (!started)
        return 1;
    while (!stop_requested)
    {
        control.process_pending(
            [this](const ControlRequest& request) {
                return handle_request(request);
            });
        std::unique_lock lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(25),
            [this] { return stop_requested.load(); });
    }
    control.process_pending(
        [this](const ControlRequest& request) {
            return handle_request(request);
        });
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
