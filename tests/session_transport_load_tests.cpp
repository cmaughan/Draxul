#include <catch2/catch_test_macros.hpp>

#include "../libs/draxul-server/src/fake_terminal_runtime.h"
#include "../libs/draxul-server/src/remote_terminal_service.h"
#include "../libs/draxul-server/src/session_poll_service.h"
#include "../libs/draxul-server/src/session_stream_service.h"
#include "../libs/draxul-server/src/topology_service.h"
#include "support/temp_dir.h"

#include <draxul/async_frame_stream.h>
#include <draxul/control_plane.h>
#include <draxul/remote_terminal_client.h>
#include <draxul/server_protocol.h>
#include <draxul/session_protocol.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace draxul;
using draxul::tests::TempDir;

namespace
{

constexpr int kLoadRounds = 20;

struct LoadScenario
{
    size_t panes = 0;
    size_t attached_uis = 0;
};

struct PaneFixture
{
    std::string method_prefix;
    std::string terminal_id;
    std::string client_id;
    size_t ui_index = 0;
    uint64_t subscription_id = 0;
    std::unique_ptr<FakeTerminalRuntime> runtime;
    std::unique_ptr<RemoteTerminalService> service;
    std::unique_ptr<RemoteTerminalClient> client;
};

struct RequestSample
{
    std::string channel;
    bool ok = false;
    std::string error_code;
    uint64_t request_latency_us = 0;
    uint64_t delivery_latency_us = 0;
    uint64_t terminal_sequence = 0;
};

struct SessionPollSample
{
    bool ok = false;
    uint64_t request_serial = 0;
    std::string error_code;
    std::string error_message;
    std::optional<SessionPollResponse> response;
};

struct BatchedUiState
{
    std::string client_id;
    uint64_t topology_revision = 0;
    uint64_t agent_revision = 0;
    uint64_t next_request_serial = 1;
};

template <typename T>
bool pump_until_ready(ControlServer& server,
    const ControlServer::Handler& handler,
    std::vector<std::future<T>>& futures,
    std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        server.process_pending(handler);
        const bool ready = std::ranges::all_of(futures,
            [](std::future<T>& future) {
                return future.wait_for(std::chrono::milliseconds::zero())
                    == std::future_status::ready;
            });
        if (ready)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    server.process_pending(handler);
    return std::ranges::all_of(futures,
        [](std::future<T>& future) {
            return future.wait_for(std::chrono::milliseconds::zero())
                == std::future_status::ready;
        });
}

uint64_t elapsed_us(std::chrono::steady_clock::time_point started_at,
    std::chrono::steady_clock::time_point finished_at)
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            finished_at - started_at)
            .count());
}

nlohmann::json latency_summary(std::vector<uint64_t> samples)
{
    if (samples.empty())
    {
        return {
            { "samples", 0 },
            { "p50", 0 },
            { "p95", 0 },
            { "max", 0 },
        };
    }
    std::ranges::sort(samples);
    const auto percentile = [&](double value) {
        const size_t index = std::min(samples.size() - 1,
            static_cast<size_t>(std::ceil(value * samples.size())) - 1);
        return samples[index];
    };
    return {
        { "samples", samples.size() },
        { "p50", percentile(0.50) },
        { "p95", percentile(0.95) },
        { "max", samples.back() },
    };
}

nlohmann::json control_metrics_json(
    const ControlServerMetricsSnapshot& metrics)
{
    nlohmann::json methods = nlohmann::json::object();
    for (const auto& method : metrics.methods)
    {
        methods[method.method] = {
            { "requests", method.requests },
            { "failures", method.failures },
            { "queue_total_us", method.queue_time.total_us },
            { "queue_max_us", method.queue_time.max_us },
            { "dispatch_total_us", method.dispatch_time.total_us },
            { "dispatch_max_us", method.dispatch_time.max_us },
            { "response_total_us", method.response_time.total_us },
            { "response_max_us", method.response_time.max_us },
        };
    }
    nlohmann::json failures = nlohmann::json::array();
    for (const auto& failure : metrics.transport_failures)
    {
        failures.push_back({
            { "operation", failure.operation },
            { "stage", failure.stage },
            { "native_domain", failure.native_domain },
            { "classification", failure.classification },
            { "native_code", failure.native_code },
            { "count", failure.count },
        });
    }
    return {
        { "listener_capacity", metrics.listener_capacity },
        { "accepted_connections", metrics.accepted_connections },
        { "active_connections", metrics.active_connections },
        { "peak_connections", metrics.peak_connections },
        { "requests", metrics.requests },
        { "failed_requests", metrics.failed_requests },
        { "invalid_frames", metrics.invalid_frames },
        { "methods", std::move(methods) },
        { "transport_failures", std::move(failures) },
    };
}

nlohmann::json run_load_scenario(const LoadScenario scenario)
{
    INFO("panes=" << scenario.panes
                   << " attached_uis=" << scenario.attached_uis);
    REQUIRE(scenario.panes > 0);
    REQUIRE(scenario.attached_uis > 0);

    TempDir temp("draxul-session-load");
    const std::string control_id
        = namespaced_control_id(kServerControlId, temp.path);
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        control_id, temp.path, [] {}, &start_error));

    std::vector<PaneFixture> panes;
    panes.reserve(scenario.panes);
    for (size_t index = 0; index < scenario.panes; ++index)
    {
        const std::string suffix = std::to_string(index);
        const std::string prefix = "load-" + suffix;
        const std::string terminal_id = "load-terminal-" + suffix;
        const std::string client_id = "load-ui-"
            + std::to_string(index % scenario.attached_uis);
        auto runtime = std::make_unique<FakeTerminalRuntime>();
        auto service = std::make_unique<RemoteTerminalService>(
            RemoteTerminalServiceOptions{
                .method_prefix = prefix,
                .server_epoch = "load-epoch",
                .pane_id = "load-pane-" + suffix,
                .terminal_id = terminal_id,
                .name = "Load pane " + suffix,
            },
            *runtime);
        auto client = std::make_unique<RemoteTerminalClient>(
            RemoteTerminalClientOptions{
                .runtime_directory = temp.path,
                .client_id = client_id,
                .expected_server_epoch = "load-epoch",
                .method_prefix = prefix,
                .terminal_id = terminal_id,
            });
        panes.push_back({
            .method_prefix = prefix,
            .terminal_id = terminal_id,
            .client_id = client_id,
            .ui_index = index % scenario.attached_uis,
            .subscription_id = index + 1,
            .runtime = std::move(runtime),
            .service = std::move(service),
            .client = std::move(client),
        });
    }

    std::map<std::string, uint64_t> handled_methods;
    const ControlServer::Handler handler
        = [&](const ControlRequest& request) {
              ++handled_methods[request.method];
              if (request.method == "topology.poll"
                  || request.method == "agent.poll")
              {
                  return ControlMethodResult::success({
                      { "revision",
                          request.params.value("revision", uint64_t{ 0 }) },
                  });
              }
              for (const auto& pane : panes)
              {
                  if (pane.service->handles(request.method))
                  {
                      return pane.service->handle(
                          request.method, request.params);
                  }
              }
              return ControlMethodResult::error(
                  "unknown_method", "Unexpected load-test method.");
          };

    // Attach serially so setup cannot be confused with the steady polling
    // load. The measured rounds below are the intentionally concurrent part.
    for (auto& pane : panes)
    {
        std::vector<std::future<RequestSample>> attach;
        attach.push_back(std::async(std::launch::async,
            [client = pane.client.get()] {
                const auto started_at = std::chrono::steady_clock::now();
                std::string error;
                const bool ok = client->attach(error);
                const auto finished_at = std::chrono::steady_clock::now();
                return RequestSample{
                    .channel = "terminal.attach",
                    .ok = ok,
                    .error_code = ok ? std::string{}
                                     : client->last_error_code(),
                    .request_latency_us
                    = elapsed_us(started_at, finished_at),
                    .delivery_latency_us
                    = elapsed_us(started_at, finished_at),
                    .terminal_sequence = ok
                        ? client->projection().version().sequence
                        : 0,
                };
            }));
        REQUIRE(pump_until_ready(
            server, handler, attach, std::chrono::seconds(6)));
        const RequestSample result = attach.front().get();
        INFO(result.error_code);
        REQUIRE(result.ok);
    }
    std::vector<uint64_t> expected_final_sequences;
    expected_final_sequences.reserve(panes.size());
    for (const auto& pane : panes)
    {
        expected_final_sequences.push_back(
            pane.client->projection().version().sequence
            + static_cast<uint64_t>(kLoadRounds));
    }
    std::vector<RequestSample> samples;
    samples.reserve(kLoadRounds
        * (scenario.panes + 2 * scenario.attached_uis));
    const std::clock_t cpu_started_at = std::clock();
    const auto load_started_at = std::chrono::steady_clock::now();

    for (int round = 1; round <= kLoadRounds; ++round)
    {
        // No client request is in flight at this barrier, so all terminal
        // mutation and service publication remains on the test/server thread.
        // Use the production service input path rather than directly pumping
        // FakeTerminalRuntime: the fake reports itself running from birth, so
        // the service's normal kernel pump gate is intentionally not armed.
        for (auto& pane : panes)
        {
            const auto update = pane.service->handle(
                pane.method_prefix + ".input",
                {
                    { "client_id", pane.client_id },
                    { "request_id", static_cast<uint64_t>(round) },
                    { "text", "x" },
                });
            REQUIRE(update.ok);
        }
        const auto published_at = std::chrono::steady_clock::now();

        std::vector<std::future<RequestSample>> requests;
        requests.reserve(
            scenario.panes + 2 * scenario.attached_uis);
        for (auto& pane : panes)
        {
            requests.push_back(std::async(std::launch::async,
                [client = pane.client.get(), published_at] {
                    const auto started_at
                        = std::chrono::steady_clock::now();
                    bool changed = false;
                    std::string error;
                    const bool ok = client->poll(changed, error);
                    const auto finished_at
                        = std::chrono::steady_clock::now();
                    return RequestSample{
                        .channel = "terminal",
                        .ok = ok,
                        .error_code = ok ? std::string{}
                                         : client->last_error_code(),
                        .request_latency_us
                        = elapsed_us(started_at, finished_at),
                        .delivery_latency_us
                        = elapsed_us(published_at, finished_at),
                        .terminal_sequence = ok
                            ? client->projection().version().sequence
                            : 0,
                    };
                }));
        }
        for (size_t ui = 0; ui < scenario.attached_uis; ++ui)
        {
            for (const std::string_view method : {
                     std::string_view("topology.poll"),
                     std::string_view("agent.poll") })
            {
                requests.push_back(std::async(std::launch::async,
                    [&, method, ui, round, published_at] {
                        const auto started_at
                            = std::chrono::steady_clock::now();
                        const auto response = ControlClient::request(
                            control_id, temp.path, method,
                            {
                                { "client_id",
                                    "load-ui-" + std::to_string(ui) },
                                { "revision",
                                    static_cast<uint64_t>(round) },
                            });
                        const auto finished_at
                            = std::chrono::steady_clock::now();
                        return RequestSample{
                            .channel = method == "topology.poll"
                                ? "topology"
                                : "agent",
                            .ok = response.ok,
                            .error_code = response.error_code,
                            .request_latency_us
                            = elapsed_us(started_at, finished_at),
                            .delivery_latency_us
                            = elapsed_us(published_at, finished_at),
                        };
                    }));
            }
        }

        REQUIRE(pump_until_ready(
            server, handler, requests, std::chrono::seconds(8)));
        for (auto& request : requests)
            samples.push_back(request.get());
    }

    const auto load_finished_at = std::chrono::steady_clock::now();
    const std::clock_t cpu_finished_at = std::clock();
    const uint64_t load_wall_us
        = elapsed_us(load_started_at, load_finished_at);
    const double cpu_ms = cpu_started_at == static_cast<std::clock_t>(-1)
            || cpu_finished_at == static_cast<std::clock_t>(-1)
        ? 0.0
        : 1000.0 * static_cast<double>(
                       cpu_finished_at - cpu_started_at)
            / static_cast<double>(CLOCKS_PER_SEC);

    const size_t expected_load_requests = static_cast<size_t>(kLoadRounds)
        * (scenario.panes + 2 * scenario.attached_uis);
    REQUIRE(samples.size() == expected_load_requests);
    const auto handled_methods_before_recovery = handled_methods;

    std::map<std::string, uint64_t> failures;
    std::map<std::string, std::vector<uint64_t>> request_latencies;
    std::map<std::string, std::vector<uint64_t>> delivery_latencies;
    size_t successes = 0;
    for (const RequestSample& sample : samples)
    {
        request_latencies[sample.channel].push_back(
            sample.request_latency_us);
        delivery_latencies[sample.channel].push_back(
            sample.delivery_latency_us);
        if (sample.ok)
            ++successes;
        else
            ++failures[sample.error_code.empty()
                    ? "unclassified"
                    : sample.error_code];
    }

    // Failures are data in the legacy baseline, not a test failure. Prove the
    // fixture remains usable by converging every projection after the timed
    // burst. Recover one pane at a time so this check does not recreate the
    // endpoint pressure that the preceding load is intended to measure.
    std::vector<bool> converged(scenario.panes, false);
    for (size_t index = 0; index < panes.size(); ++index)
    {
        for (int recovery_attempt = 0;
             recovery_attempt < 3 && !converged[index];
             ++recovery_attempt)
        {
            std::vector<std::future<RequestSample>> recovery;
            recovery.push_back(std::async(std::launch::async,
                [client = panes[index].client.get()] {
                    const auto started_at
                        = std::chrono::steady_clock::now();
                    bool changed = false;
                    std::string error;
                    const bool ok = client->poll(changed, error);
                    const auto finished_at
                        = std::chrono::steady_clock::now();
                    return RequestSample{
                        .channel = "terminal.recovery",
                        .ok = ok,
                        .error_code = ok ? std::string{}
                                         : client->last_error_code(),
                        .request_latency_us
                        = elapsed_us(started_at, finished_at),
                        .delivery_latency_us
                        = elapsed_us(started_at, finished_at),
                        .terminal_sequence = ok
                            ? client->projection().version().sequence
                            : 0,
                    };
                }));
            REQUIRE(pump_until_ready(
                server, handler, recovery, std::chrono::seconds(6)));
            const RequestSample result = recovery.front().get();
            converged[index] = result.ok
                && result.terminal_sequence
                    == expected_final_sequences[index];
        }
    }
    for (size_t index = 0; index < converged.size(); ++index)
    {
        INFO("pane=" << index
                     << " client_sequence="
                     << panes[index].client->projection().version().sequence
                     << " expected_sequence="
                     << expected_final_sequences[index]);
        REQUIRE(converged[index]);
    }

    nlohmann::json request_latency_json = nlohmann::json::object();
    nlohmann::json delivery_latency_json = nlohmann::json::object();
    for (auto& [channel, values] : request_latencies)
        request_latency_json[channel] = latency_summary(std::move(values));
    for (auto& [channel, values] : delivery_latencies)
        delivery_latency_json[channel] = latency_summary(std::move(values));

    const size_t total_attempted_requests
        = scenario.panes + expected_load_requests;
    nlohmann::json result{
        { "panes", scenario.panes },
        { "attached_uis", scenario.attached_uis },
        { "rounds", kLoadRounds },
        { "setup_attach_requests", scenario.panes },
        { "load_attempted_requests", expected_load_requests },
        { "total_attempted_requests", total_attempted_requests },
        { "load_successes", successes },
        { "load_failures", failures },
        { "load_wall_ms", static_cast<double>(load_wall_us) / 1000.0 },
        { "load_cpu_ms", cpu_ms },
        { "load_requests_per_second", load_wall_us == 0
                ? 0.0
                : static_cast<double>(expected_load_requests)
                    * 1'000'000.0
                    / static_cast<double>(load_wall_us) },
        { "handled_methods_before_recovery",
            handled_methods_before_recovery },
        { "request_latency_us", std::move(request_latency_json) },
        { "delivery_latency_us", std::move(delivery_latency_json) },
        { "control_transport",
            control_metrics_json(server.metrics_snapshot()) },
        { "final_projection_convergence", true },
    };
    server.stop();
    return result;
}

SessionPollRequest make_session_poll_request(
    BatchedUiState& ui,
    const std::vector<PaneFixture>& panes,
    std::string_view server_epoch = "load-epoch")
{
    SessionPollRequest request{
        .request_serial = ui.next_request_serial++,
        .server_epoch = std::string(server_epoch),
        .topology_after_revision = ui.topology_revision,
        .agent_after_revision = ui.agent_revision,
    };
    for (const auto& pane : panes)
    {
        if (pane.client_id != ui.client_id)
            continue;
        SessionTerminalSubscription subscription{
            .subscription_id = pane.subscription_id,
            .terminal_id = pane.terminal_id,
            .visibility_generation = 1,
            .visible = true,
        };
        if (pane.client->projection().attached())
        {
            const auto& version
                = pane.client->projection().version();
            subscription.cursor = SessionTerminalCursor{
                .generation = version.generation,
                .after_sequence = version.sequence,
            };
        }
        request.terminals.push_back(std::move(subscription));
    }
    return request;
}

bool apply_session_poll_response(
    BatchedUiState& ui, std::vector<PaneFixture>& panes,
    const SessionPollResponse& response, std::string& error,
    std::string_view server_epoch = "load-epoch")
{
    if (response.server_epoch != server_epoch)
    {
        error = "Batched load response used the wrong server epoch.";
        return false;
    }
    if (!response.topology.error_code.empty()
        || !response.agents.error_code.empty())
    {
        error = !response.topology.error_code.empty()
            ? response.topology.error_code
            : response.agents.error_code;
        return false;
    }
    if (response.topology.snapshot)
        ui.topology_revision = response.topology.snapshot->revision;
    if (response.agents.snapshot)
        ui.agent_revision = response.agents.snapshot->revision;

    for (const auto& batch : response.terminals)
    {
        auto found = std::ranges::find_if(panes,
            [&](const PaneFixture& pane) {
                return pane.client_id == ui.client_id
                    && pane.subscription_id
                        == batch.subscription_id;
            });
        if (found == panes.end()
            || found->terminal_id != batch.terminal_id
            || batch.visibility_generation != 1
            || !batch.error_code.empty())
        {
            error = batch.error_code.empty()
                ? "Batched terminal identity did not match its subscription."
                : batch.error_code;
            return false;
        }
        if (batch.attach
            && !found->client->accept_attach(*batch.attach, error))
        {
            return false;
        }
        if (!batch.events.empty())
        {
            bool changed = false;
            if (!found->client->accept_events(
                    batch.events, changed, error)
                || !changed)
            {
                return false;
            }
        }
    }
    error.clear();
    return true;
}

struct RawSessionStream
{
    BatchedUiState ui;
    std::unique_ptr<AsyncFrameStreamConnection> connection;
    uint64_t last_frame_serial = 0;
    size_t max_queue_bytes = 0;
};

std::optional<SessionStreamOpenResponse> open_stream_over_control(
    ControlServer& control, const ControlServer::Handler& handler,
    std::string_view control_id,
    const std::filesystem::path& runtime_directory,
    std::string_view client_id,
    SessionPollRequest initial_poll,
    std::string& error)
{
    SessionStreamOpenRequest open_request{
        .server_epoch = "stream-epoch",
        .session_id = "default",
        .poll = std::move(initial_poll),
    };
    nlohmann::json params
        = session_stream_open_request_to_json(open_request);
    params["client_id"] = client_id;
    std::vector<std::future<ControlClientResult>> request;
    request.push_back(std::async(std::launch::async,
        [control_id = std::string(control_id), runtime_directory,
            params = std::move(params)]() mutable {
            return ControlClient::request(control_id,
                runtime_directory, "session.stream.open",
                std::move(params));
        }));
    if (!pump_until_ready(
            control, handler, request, std::chrono::seconds(5)))
    {
        error = "Timed out opening the Session event stream.";
        return std::nullopt;
    }
    ControlClientResult result = request.front().get();
    if (!result.ok)
    {
        error = result.error_code + ": " + result.error_message;
        return std::nullopt;
    }
    auto response = session_stream_open_response_from_json(
        result.result, error);
    if (!response)
        return std::nullopt;
    if (response->server_epoch != "stream-epoch")
    {
        error = "Session stream open returned the wrong server epoch.";
        return std::nullopt;
    }
    return response;
}

bool connect_raw_stream(RawSessionStream& stream,
    SessionStreamOpenResponse response, std::string& error)
{
    AsyncFrameStreamError transport_error;
    stream.connection = AsyncFrameStreamClient::connect(
        response.endpoint, std::chrono::seconds(2), transport_error);
    if (!stream.connection)
    {
        error = transport_error.code + ": " + transport_error.message;
        return false;
    }
    stream.max_queue_bytes = response.max_queue_bytes;
    const std::string connect_frame
        = session_stream_client_frame_to_json({
              .kind = SessionStreamClientFrameKind::Connect,
              .connect = SessionStreamConnectRequest{
                  .server_epoch = response.server_epoch,
                  .ticket = std::move(response.ticket),
              },
          }).dump();
    if (!stream.connection->write_frame(
            connect_frame, {}, transport_error))
    {
        error = transport_error.code + ": " + transport_error.message;
        stream.connection.reset();
        return false;
    }
    return true;
}

bool write_stream_update(RawSessionStream& stream,
    SessionPollRequest request, std::string& error)
{
    AsyncFrameStreamError transport_error;
    const std::string bytes = session_stream_client_frame_to_json({
        .kind = SessionStreamClientFrameKind::Update,
        .update = SessionStreamUpdate{
            .poll = std::move(request),
        },
    }).dump();
    if (!stream.connection
        || !stream.connection->write_frame(
            bytes, {}, transport_error))
    {
        error = transport_error.code + ": " + transport_error.message;
        return false;
    }
    return true;
}

bool read_stream_frame(RawSessionStream& stream,
    SessionStreamService& service,
    const SessionStreamService::Poll& poll,
    SessionStreamServerFrame& frame,
    std::chrono::milliseconds timeout,
    std::string& error)
{
    struct ReadResult
    {
        bool ok = false;
        std::string bytes;
        AsyncFrameStreamError error;
    };
    auto pending = std::async(std::launch::async,
        [connection = stream.connection.get()] {
            ReadResult result;
            result.ok = connection->read_frame(
                result.bytes, {}, result.error);
            return result;
        });
    const auto deadline
        = std::chrono::steady_clock::now() + timeout;
    while (pending.wait_for(std::chrono::milliseconds::zero())
            != std::future_status::ready
        && std::chrono::steady_clock::now() < deadline)
    {
        service.pump(poll);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (pending.wait_for(std::chrono::milliseconds::zero())
        != std::future_status::ready)
    {
        if (stream.connection)
            stream.connection->close();
        pending.wait();
        error = "Timed out waiting for a Session event stream frame.";
        return false;
    }
    ReadResult result = pending.get();
    if (!result.ok)
    {
        error = result.error.code + ": " + result.error.message;
        return false;
    }
    const auto encoded
        = nlohmann::json::parse(result.bytes, nullptr, false);
    auto parsed = encoded.is_discarded()
        ? std::nullopt
        : session_stream_server_frame_from_json(encoded, error);
    if (!parsed)
    {
        if (error.empty())
            error = "Session stream server frame was not valid JSON.";
        return false;
    }
    if (parsed->server_epoch != "stream-epoch"
        || parsed->frame_serial != stream.last_frame_serial + 1)
    {
        error = "Session stream frame identity or ordering changed.";
        return false;
    }
    stream.last_frame_serial = parsed->frame_serial;
    frame = std::move(*parsed);
    return true;
}

bool read_stream_events(RawSessionStream& stream,
    SessionStreamService& service,
    const SessionStreamService::Poll& poll,
    SessionStreamServerFrame& frame,
    std::chrono::milliseconds timeout,
    std::string& error)
{
    const auto deadline
        = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto remaining
            = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
        if (!read_stream_frame(stream, service, poll, frame,
                std::max(remaining, std::chrono::milliseconds(1)),
                error))
        {
            return false;
        }
        if (frame.kind == SessionStreamServerFrameKind::Events)
            return true;
        if (frame.kind == SessionStreamServerFrameKind::Error)
        {
            error = frame.error_code + ": " + frame.error_message;
            return false;
        }
    }
    error = "Timed out waiting for Session stream Events.";
    return false;
}

nlohmann::json run_batched_load_scenario(
    const LoadScenario scenario)
{
    INFO("batched panes=" << scenario.panes
                           << " attached_uis="
                           << scenario.attached_uis);
    REQUIRE(scenario.panes > 0);
    REQUIRE(scenario.attached_uis > 0);

    TempDir temp("draxul-session-batched-load");
    const std::string control_id
        = namespaced_control_id(kServerControlId, temp.path);
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        control_id, temp.path, [] {}, &start_error));

    std::vector<PaneFixture> panes;
    panes.reserve(scenario.panes);
    for (size_t index = 0; index < scenario.panes; ++index)
    {
        const std::string suffix = std::to_string(index);
        const std::string prefix = "batch-" + suffix;
        const std::string terminal_id
            = "batch-terminal-" + suffix;
        const size_t ui_index
            = index % scenario.attached_uis;
        const std::string client_id
            = "batch-ui-" + std::to_string(ui_index);
        auto runtime = std::make_unique<FakeTerminalRuntime>();
        auto service = std::make_unique<RemoteTerminalService>(
            RemoteTerminalServiceOptions{
                .method_prefix = prefix,
                .server_epoch = "load-epoch",
                .pane_id = "batch-pane-" + suffix,
                .terminal_id = terminal_id,
                .name = "Batched load pane " + suffix,
            },
            *runtime);
        auto client = std::make_unique<RemoteTerminalClient>(
            RemoteTerminalClientOptions{
                .runtime_directory = temp.path,
                .client_id = client_id,
                .expected_server_epoch = "load-epoch",
                .method_prefix = prefix,
                .terminal_id = terminal_id,
            });
        panes.push_back({
            .method_prefix = prefix,
            .terminal_id = terminal_id,
            .client_id = client_id,
            .ui_index = ui_index,
            .subscription_id = index + 1,
            .runtime = std::move(runtime),
            .service = std::move(service),
            .client = std::move(client),
        });
    }
    std::vector<BatchedUiState> uis;
    uis.reserve(scenario.attached_uis);
    for (size_t index = 0;
         index < scenario.attached_uis; ++index)
    {
        uis.push_back({
            .client_id = "batch-ui-" + std::to_string(index),
        });
    }

    TopologyService topology_service("default");
    TopologySnapshot topology = topology_service.snapshot();
    ServerAgentSnapshot agents{
        .revision = 1,
        .session_id = "default",
    };
    std::vector<SessionPollTerminalView> terminal_views;
    terminal_views.reserve(panes.size());
    for (auto& pane : panes)
    {
        terminal_views.push_back({
            .terminal_id = pane.terminal_id,
            .service = pane.service.get(),
        });
    }
    SessionPollService session_poll("load-epoch");
    std::map<std::string, uint64_t> handled_methods;
    const ControlServer::Handler handler
        = [&](const ControlRequest& control_request) {
              ++handled_methods[control_request.method];
              if (control_request.method != "session.poll")
              {
                  return ControlMethodResult::error(
                      "unknown_method",
                      "Unexpected batched load-test method.");
              }
              const std::string client_id
                  = control_request.params.value(
                      "client_id", std::string{});
              return session_poll.handle(
                  control_request.params, client_id,
                  topology, agents, terminal_views);
          };

    const auto poll_uis = [&]() -> bool {
        std::vector<std::future<SessionPollSample>> requests;
        requests.reserve(uis.size());
        for (auto& ui : uis)
        {
            SessionPollRequest request
                = make_session_poll_request(ui, panes);
            const uint64_t request_serial
                = request.request_serial;
            nlohmann::json params
                = session_poll_request_to_json(request);
            params["client_id"] = ui.client_id;
            params["session_id"] = "default";
            requests.push_back(std::async(std::launch::async,
                [&, params = std::move(params),
                    request_serial]() mutable {
                    const auto result = ControlClient::request(
                        control_id, temp.path,
                        "session.poll", std::move(params));
                    SessionPollSample sample{
                        .ok = result.ok,
                        .request_serial = request_serial,
                        .error_code = result.error_code,
                        .error_message = result.error_message,
                    };
                    if (!result.ok)
                        return sample;
                    std::string parse_error;
                    sample.response
                        = session_poll_response_from_json(
                            result.result, parse_error);
                    if (!sample.response)
                    {
                        sample.ok = false;
                        sample.error_code
                            = "invalid_session_poll_response";
                        sample.error_message
                            = std::move(parse_error);
                    }
                    return sample;
                }));
        }
        REQUIRE(pump_until_ready(
            server, handler, requests,
            std::chrono::seconds(8)));
        bool any_more = false;
        for (size_t index = 0;
             index < requests.size(); ++index)
        {
            SessionPollSample sample
                = requests[index].get();
            INFO(sample.error_code << ": "
                                   << sample.error_message);
            REQUIRE(sample.ok);
            REQUIRE(sample.response);
            REQUIRE(sample.response->request_serial
                == sample.request_serial);
            std::string apply_error;
            const bool applied = apply_session_poll_response(
                uis[index], panes, *sample.response,
                apply_error);
            INFO(apply_error);
            REQUIRE(applied);
            any_more = any_more
                || sample.response->more;
        }
        return any_more;
    };

    // One negotiated setup poll per UI establishes every subscription and
    // supplies authoritative channel snapshots. Setup is deliberately
    // excluded from the recurring request-count assertion below.
    bool setup_complete = false;
    for (size_t attempt = 0;
         attempt < kSessionPollMaxSubscriptions
         && !setup_complete; ++attempt)
    {
        const bool more = poll_uis();
        setup_complete = !more
            && std::ranges::all_of(panes,
                [](const PaneFixture& pane) {
                    return pane.client->projection()
                        .attached();
                });
    }
    REQUIRE(setup_complete);
    const uint64_t setup_session_poll_requests
        = handled_methods["session.poll"];
    REQUIRE(setup_session_poll_requests
        >= scenario.attached_uis);
    handled_methods.clear();

    std::vector<uint64_t> expected_final_sequences;
    expected_final_sequences.reserve(panes.size());
    for (const auto& pane : panes)
    {
        expected_final_sequences.push_back(
            pane.client->projection().version().sequence
            + static_cast<uint64_t>(kLoadRounds));
    }

    for (int round = 1; round <= kLoadRounds; ++round)
    {
        for (auto& pane : panes)
        {
            const auto update = pane.service->handle(
                pane.method_prefix + ".input",
                {
                    { "client_id", pane.client_id },
                    { "request_id",
                        static_cast<uint64_t>(round) },
                    { "text", "x" },
                });
            REQUIRE(update.ok);
        }
        ++topology.revision;
        ++agents.revision;
        CHECK_FALSE(poll_uis());
    }

    const size_t expected_recurring_requests
        = static_cast<size_t>(kLoadRounds)
        * scenario.attached_uis;
    const size_t legacy_equivalent_recurring_requests
        = static_cast<size_t>(kLoadRounds)
        * (scenario.panes + 2 * scenario.attached_uis);
    REQUIRE(expected_recurring_requests
        < legacy_equivalent_recurring_requests);
    REQUIRE(handled_methods["session.poll"]
        == expected_recurring_requests);
    REQUIRE(handled_methods["topology.poll"] == 0);
    REQUIRE(handled_methods["agent.poll"] == 0);
    for (const auto& pane : panes)
        REQUIRE(handled_methods[pane.method_prefix + ".poll"] == 0);

    for (size_t index = 0; index < panes.size(); ++index)
    {
        INFO("batched pane=" << index
                              << " client_sequence="
                              << panes[index]
                                     .client->projection()
                                     .version().sequence
                              << " expected_sequence="
                              << expected_final_sequences[index]);
        REQUIRE(panes[index].client->projection()
                  .version().sequence
            == expected_final_sequences[index]);
    }
    for (const auto& ui : uis)
    {
        REQUIRE(ui.topology_revision == topology.revision);
        REQUIRE(ui.agent_revision == agents.revision);
    }

    nlohmann::json result{
        { "panes", scenario.panes },
        { "attached_uis", scenario.attached_uis },
        { "rounds", kLoadRounds },
        { "capability", "session-poll-v1" },
        { "setup_session_poll_requests",
            setup_session_poll_requests },
        { "recurring_session_poll_requests",
            expected_recurring_requests },
        { "legacy_equivalent_recurring_requests",
            legacy_equivalent_recurring_requests },
        { "legacy_terminal_poll_requests", 0 },
        { "legacy_topology_poll_requests", 0 },
        { "legacy_agent_poll_requests", 0 },
        { "final_topology_revision", topology.revision },
        { "final_agent_revision", agents.revision },
        { "control_transport",
            control_metrics_json(server.metrics_snapshot()) },
        { "final_projection_convergence", true },
    };
    server.stop();
    return result;
}

nlohmann::json run_stream_load_scenario(
    const LoadScenario scenario, bool exercise_stalled_reader)
{
    INFO("stream panes=" << scenario.panes
                          << " attached_uis="
                          << scenario.attached_uis);
    REQUIRE(scenario.panes > 0);
    REQUIRE(scenario.attached_uis > 0);

    TempDir temp("draxul-session-stream-load");
    const std::string control_id
        = namespaced_control_id(kServerControlId, temp.path);
    ControlServer control;
    std::string error;
    REQUIRE(control.start(control_id, temp.path, [] {}, &error));
    constexpr size_t kTestStreamQueueBytes = 1024 * 1024;
    SessionStreamService stream_service({
        .runtime_directory = temp.path,
        .server_epoch = "stream-epoch",
        .heartbeat_interval = std::chrono::milliseconds(100),
        .max_queue_bytes = kTestStreamQueueBytes,
    });
    REQUIRE(stream_service.start(error));

    std::vector<PaneFixture> panes;
    panes.reserve(scenario.panes);
    for (size_t index = 0; index < scenario.panes; ++index)
    {
        const std::string suffix = std::to_string(index);
        const size_t ui_index = index % scenario.attached_uis;
        const std::string client_id
            = "stream-load-ui-" + std::to_string(ui_index);
        auto runtime = std::make_unique<FakeTerminalRuntime>();
        auto service = std::make_unique<RemoteTerminalService>(
            RemoteTerminalServiceOptions{
                .method_prefix = "stream-load-" + suffix,
                .server_epoch = "stream-epoch",
                .pane_id = "stream-load-pane-" + suffix,
                .terminal_id = "stream-load-terminal-" + suffix,
                .name = "Stream load pane " + suffix,
            },
            *runtime);
        auto client = std::make_unique<RemoteTerminalClient>(
            RemoteTerminalClientOptions{
                .runtime_directory = temp.path,
                .client_id = client_id,
                .expected_server_epoch = "stream-epoch",
                .method_prefix = "stream-load-" + suffix,
                .terminal_id = "stream-load-terminal-" + suffix,
            });
        panes.push_back({
            .method_prefix = "stream-load-" + suffix,
            .terminal_id = "stream-load-terminal-" + suffix,
            .client_id = client_id,
            .ui_index = ui_index,
            .subscription_id = index + 1,
            .runtime = std::move(runtime),
            .service = std::move(service),
            .client = std::move(client),
        });
    }
    std::vector<SessionPollTerminalView> terminal_views;
    terminal_views.reserve(panes.size());
    for (auto& pane : panes)
    {
        terminal_views.push_back({
            .terminal_id = pane.terminal_id,
            .service = pane.service.get(),
        });
    }

    TopologyService topology_service("default");
    TopologySnapshot topology = topology_service.snapshot();
    ServerAgentSnapshot agents{
        .revision = 1,
        .session_id = "default",
    };
    SessionPollService poll_service("stream-epoch");
    size_t stream_poll_calls = 0;
    const SessionStreamService::Poll stream_poll
        = [&](std::string_view session_id,
              std::string_view client_id,
              const SessionPollRequest& request,
              size_t payload_budget) {
              ++stream_poll_calls;
              if (session_id != "default")
              {
                  return ControlMethodResult::error(
                      "invalid_session", "Unexpected stream Session.");
              }
              return poll_service.handle(
                  session_poll_request_to_json(request), client_id,
                  topology, agents, terminal_views, payload_budget);
          };
    std::map<std::string, size_t> handled_methods;
    const ControlServer::Handler control_handler
        = [&](const ControlRequest& request) {
              ++handled_methods[request.method];
              if (request.method == "session.stream.open")
              {
                  return stream_service.open(request.params,
                      request.params.value(
                          "client_id", std::string{}));
              }
              if (request.method == "server.status")
              {
                  return ControlMethodResult::success({
                      { "state", "ready" },
                      { "stream_connections",
                          stream_service.connection_count() },
                  });
              }
              return ControlMethodResult::error(
                  "unknown_method", "Unexpected stream load method.");
          };

    std::vector<RawSessionStream> streams;
    streams.reserve(scenario.attached_uis);
    for (size_t index = 0;
         index < scenario.attached_uis; ++index)
    {
        RawSessionStream stream{
            .ui = {
                .client_id = "stream-load-ui-"
                    + std::to_string(index),
            },
        };
        auto opened = open_stream_over_control(control,
            control_handler, control_id, temp.path,
            stream.ui.client_id,
            make_session_poll_request(
                stream.ui, panes, "stream-epoch"), error);
        INFO(error);
        REQUIRE(opened);
        REQUIRE(connect_raw_stream(
            stream, std::move(*opened), error));
        REQUIRE(stream.max_queue_bytes == kTestStreamQueueBytes);
        streams.push_back(std::move(stream));
    }

    size_t max_frame_bytes = 0;
    for (auto& stream : streams)
    {
        bool attached = false;
        for (size_t attempt = 0;
             attempt < kSessionPollMaxSubscriptions
             && !attached; ++attempt)
        {
            SessionStreamServerFrame frame;
            REQUIRE(read_stream_events(stream, stream_service,
                stream_poll, frame, std::chrono::seconds(4), error));
            REQUIRE(frame.kind
                == SessionStreamServerFrameKind::Events);
            REQUIRE(frame.events);
            max_frame_bytes = std::max(max_frame_bytes,
                session_stream_server_frame_to_json(frame).dump().size());
            REQUIRE(apply_session_poll_response(
                stream.ui, panes, *frame.events, error,
                "stream-epoch"));
            attached = !frame.events->more
                && std::ranges::all_of(panes,
                    [&](const PaneFixture& pane) {
                        return pane.client_id
                                != stream.ui.client_id
                            || pane.client->projection().attached();
                    });
            if (!attached)
            {
                REQUIRE(write_stream_update(stream,
                    make_session_poll_request(
                        stream.ui, panes, "stream-epoch"), error));
            }
        }
        REQUIRE(attached);
    }
    REQUIRE(stream_service.connection_count()
        == scenario.attached_uis);
    const size_t steady_stream_connections
        = stream_service.connection_count();

    constexpr int kStreamLoadRounds = 4;
    std::vector<uint64_t> expected_sequences;
    expected_sequences.reserve(panes.size());
    for (const auto& pane : panes)
    {
        expected_sequences.push_back(
            pane.client->projection().version().sequence
            + kStreamLoadRounds);
    }
    for (int round = 1; round <= kStreamLoadRounds; ++round)
    {
        for (auto& pane : panes)
        {
            REQUIRE(pane.service->handle(
                pane.method_prefix + ".input",
                {
                    { "client_id", pane.client_id },
                    { "request_id", static_cast<uint64_t>(round) },
                    { "text", "x" },
                }).ok);
        }
        ++topology.revision;
        ++agents.revision;
        for (auto& stream : streams)
        {
            REQUIRE(write_stream_update(stream,
                make_session_poll_request(
                    stream.ui, panes, "stream-epoch"), error));
        }
        for (auto& stream : streams)
        {
            SessionStreamServerFrame frame;
            REQUIRE(read_stream_events(stream, stream_service,
                stream_poll, frame, std::chrono::seconds(4), error));
            REQUIRE(frame.kind
                == SessionStreamServerFrameKind::Events);
            REQUIRE(frame.events);
            max_frame_bytes = std::max(max_frame_bytes,
                session_stream_server_frame_to_json(frame).dump().size());
            REQUIRE(apply_session_poll_response(
                stream.ui, panes, *frame.events, error,
                "stream-epoch"));
        }
    }
    for (size_t index = 0; index < panes.size(); ++index)
    {
        REQUIRE(panes[index].client->projection()
                    .version()
                    .sequence
            == expected_sequences[index]);
    }
    for (const auto& stream : streams)
    {
        REQUIRE(stream.ui.topology_revision == topology.revision);
        REQUIRE(stream.ui.agent_revision == agents.revision);
    }

    bool stalled_reader_disconnected = false;
    size_t backpressure_rounds = 0;
    if (exercise_stalled_reader)
    {
        REQUIRE(streams.size() == 2);
        RawSessionStream& healthy = streams.front();
        RawSessionStream& stalled = streams.back();
        bool healthy_frame_pending = false;
        for (; backpressure_rounds < 512
             && stream_service.connection_count() == 2;)
        {
            for (auto& pane : panes)
            {
                REQUIRE(pane.service->handle(
                    pane.method_prefix + ".input",
                    {
                        { "client_id", pane.client_id },
                        { "request_id",
                            static_cast<uint64_t>(
                                kStreamLoadRounds
                                + backpressure_rounds + 1) },
                        { "text", "bounded-stream-output" },
                    }).ok);
            }
            ++topology.revision;
            ++agents.revision;
            ++backpressure_rounds;
            REQUIRE(write_stream_update(healthy,
                make_session_poll_request(
                    healthy.ui, panes, "stream-epoch"), error));
            healthy_frame_pending = true;
            if (!write_stream_update(stalled,
                    make_session_poll_request(
                        stalled.ui, panes, "stream-epoch"), error))
            {
                break;
            }
            SessionStreamServerFrame frame;
            REQUIRE(read_stream_events(healthy, stream_service,
                stream_poll, frame, std::chrono::seconds(4), error));
            REQUIRE(frame.kind
                == SessionStreamServerFrameKind::Events);
            REQUIRE(frame.events);
            REQUIRE(apply_session_poll_response(
                healthy.ui, panes, *frame.events, error,
                "stream-epoch"));
            healthy_frame_pending = false;
            stream_service.pump(stream_poll);
        }
        const auto disconnect_deadline
            = std::chrono::steady_clock::now()
            + std::chrono::seconds(3);
        while (stream_service.connection_count() > 1
            && std::chrono::steady_clock::now()
                < disconnect_deadline)
        {
            stream_service.pump(stream_poll);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
        stalled_reader_disconnected
            = stream_service.connection_count() == 1;
        REQUIRE(stalled_reader_disconnected);
        REQUIRE(healthy.connection->connected());
        if (healthy_frame_pending)
        {
            SessionStreamServerFrame frame;
            REQUIRE(read_stream_events(healthy, stream_service,
                stream_poll, frame, std::chrono::seconds(4), error));
            REQUIRE(frame.kind
                == SessionStreamServerFrameKind::Events);
            REQUIRE(frame.events);
            REQUIRE(apply_session_poll_response(
                healthy.ui, panes, *frame.events, error,
                "stream-epoch"));
        }
        for (size_t index = 0; index < panes.size(); ++index)
        {
            if (panes[index].client_id != healthy.ui.client_id)
                continue;
            REQUIRE(panes[index].client->projection()
                        .version()
                        .sequence
                == expected_sequences[index]
                    + backpressure_rounds);
        }
        REQUIRE(healthy.ui.topology_revision == topology.revision);
        REQUIRE(healthy.ui.agent_revision == agents.revision);
    }

    const auto status_started_at
        = std::chrono::steady_clock::now();
    auto status = std::async(std::launch::async, [&] {
        return ControlClient::request(
            control_id, temp.path, "server.status");
    });
    const auto status_deadline
        = status_started_at + std::chrono::seconds(2);
    while (status.wait_for(std::chrono::milliseconds::zero())
            != std::future_status::ready
        && std::chrono::steady_clock::now() < status_deadline)
    {
        control.process_pending(control_handler);
        stream_service.pump(stream_poll);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(status.wait_for(std::chrono::milliseconds::zero())
        == std::future_status::ready);
    REQUIRE(status.get().ok);
    const uint64_t status_latency_us = elapsed_us(
        status_started_at, std::chrono::steady_clock::now());
    CHECK(status_latency_us < 250'000);

    REQUIRE(handled_methods["session.stream.open"]
        == scenario.attached_uis);
    REQUIRE(handled_methods["server.status"] == 1);
    REQUIRE(handled_methods["session.poll"] == 0);
    REQUIRE(handled_methods["topology.poll"] == 0);
    REQUIRE(handled_methods["agent.poll"] == 0);
    for (const auto& pane : panes)
        REQUIRE(handled_methods[pane.method_prefix + ".poll"] == 0);
    const auto control_metrics = control.metrics_snapshot();
    REQUIRE(control_metrics.accepted_connections
        == scenario.attached_uis + 1);

    nlohmann::json result{
        { "panes", scenario.panes },
        { "attached_uis", scenario.attached_uis },
        { "rounds", kStreamLoadRounds },
        { "capability", "session-stream-v1" },
        { "stream_open_requests",
            handled_methods["session.stream.open"] },
        { "steady_stream_connections",
            steady_stream_connections },
        { "final_stream_connections",
            stream_service.connection_count() },
        { "control_accepted_connections",
            control_metrics.accepted_connections },
        { "recurring_session_poll_requests", 0 },
        { "legacy_terminal_poll_requests", 0 },
        { "legacy_topology_poll_requests", 0 },
        { "legacy_agent_poll_requests", 0 },
        { "stream_poll_batches", stream_poll_calls },
        { "max_stream_frame_bytes", max_frame_bytes },
        { "stream_queue_budget_bytes", kTestStreamQueueBytes },
        { "status_latency_us", status_latency_us },
        { "stalled_reader_exercised", exercise_stalled_reader },
        { "stalled_reader_disconnected",
            stalled_reader_disconnected },
        { "backpressure_rounds", backpressure_rounds },
        { "final_projection_convergence", true },
    };
    for (auto& stream : streams)
    {
        if (stream.connection)
            stream.connection->close();
    }
    stream_service.stop();
    control.stop();
    return result;
}

} // namespace

TEST_CASE("Session event stream multiplexes terminals topology and agents without polling",
    "[session-stream][control][remote-terminal]")
{
    TempDir temp("draxul-session-stream-integration");
    const std::string control_id
        = namespaced_control_id(kServerControlId, temp.path);
    ControlServer control;
    std::string error;
    REQUIRE(control.start(control_id, temp.path, [] {}, &error));
    SessionStreamService stream_service({
        .runtime_directory = temp.path,
        .server_epoch = "stream-epoch",
        .heartbeat_interval = std::chrono::milliseconds(100),
        .max_queue_bytes = 1024 * 1024,
    });
    REQUIRE(stream_service.start(error));

    std::vector<PaneFixture> panes;
    std::vector<SessionPollTerminalView> terminal_views;
    for (size_t index = 0; index < 2; ++index)
    {
        const std::string suffix = std::to_string(index);
        auto runtime = std::make_unique<FakeTerminalRuntime>();
        auto service = std::make_unique<RemoteTerminalService>(
            RemoteTerminalServiceOptions{
                .method_prefix = "stream-" + suffix,
                .server_epoch = "stream-epoch",
                .pane_id = "stream-pane-" + suffix,
                .terminal_id = "stream-terminal-" + suffix,
                .name = "Stream pane " + suffix,
            },
            *runtime);
        auto client = std::make_unique<RemoteTerminalClient>(
            RemoteTerminalClientOptions{
                .runtime_directory = temp.path,
                .client_id = "stream-ui",
                .expected_server_epoch = "stream-epoch",
                .method_prefix = "stream-" + suffix,
                .terminal_id = "stream-terminal-" + suffix,
            });
        panes.push_back({
            .method_prefix = "stream-" + suffix,
            .terminal_id = "stream-terminal-" + suffix,
            .client_id = "stream-ui",
            .ui_index = 0,
            .subscription_id = index + 1,
            .runtime = std::move(runtime),
            .service = std::move(service),
            .client = std::move(client),
        });
    }
    for (auto& pane : panes)
    {
        terminal_views.push_back({
            .terminal_id = pane.terminal_id,
            .service = pane.service.get(),
        });
    }

    TopologyService topology_service("default");
    TopologySnapshot topology = topology_service.snapshot();
    ServerAgentSnapshot agents{
        .revision = 1,
        .session_id = "default",
    };
    SessionPollService poll_service("stream-epoch");
    size_t stream_poll_calls = 0;
    const SessionStreamService::Poll stream_poll
        = [&](std::string_view session_id,
              std::string_view client_id,
              const SessionPollRequest& request,
              size_t payload_budget) {
              ++stream_poll_calls;
              if (session_id != "default"
                  || client_id != "stream-ui")
              {
                  return ControlMethodResult::error(
                      "invalid_binding",
                      "The Session stream binding changed.");
              }
              return poll_service.handle(
                  session_poll_request_to_json(request), client_id,
                  topology, agents, terminal_views, payload_budget);
          };

    std::map<std::string, size_t> control_requests;
    const ControlServer::Handler control_handler
        = [&](const ControlRequest& request) {
              ++control_requests[request.method];
              if (request.method == "session.stream.open")
              {
                  return stream_service.open(request.params,
                      request.params.value(
                          "client_id", std::string{}));
              }
              if (request.method == "server.status")
              {
                  return ControlMethodResult::success({
                      { "state", "ready" },
                      { "stream_connections",
                          stream_service.connection_count() },
                  });
              }
              return ControlMethodResult::error(
                  "unknown_method", "Unexpected stream integration method.");
          };

    RawSessionStream stream{
        .ui = {
            .client_id = "stream-ui",
        },
    };
    auto opened = open_stream_over_control(control,
        control_handler, control_id, temp.path,
        stream.ui.client_id,
        make_session_poll_request(
            stream.ui, panes, "stream-epoch"), error);
    INFO(error);
    REQUIRE(opened);
    REQUIRE(connect_raw_stream(stream, std::move(*opened), error));

    SessionStreamServerFrame frame;
    REQUIRE(read_stream_events(stream, stream_service,
        stream_poll, frame, std::chrono::seconds(3), error));
    INFO(error);
    REQUIRE(frame.kind == SessionStreamServerFrameKind::Events);
    REQUIRE(frame.events);
    REQUIRE(apply_session_poll_response(
        stream.ui, panes, *frame.events, error,
        "stream-epoch"));
    REQUIRE(std::ranges::all_of(panes,
        [](const PaneFixture& pane) {
            return pane.client->projection().attached();
        }));
    REQUIRE(stream.ui.topology_revision == topology.revision);
    REQUIRE(stream.ui.agent_revision == agents.revision);

    std::vector<uint64_t> expected_sequences;
    for (auto& pane : panes)
    {
        expected_sequences.push_back(
            pane.client->projection().version().sequence + 1);
        REQUIRE(pane.service->handle(
            pane.method_prefix + ".input",
            {
                { "client_id", stream.ui.client_id },
                { "request_id", expected_sequences.size() },
                { "text", "stream-" + pane.terminal_id },
            }).ok);
    }
    ++topology.revision;
    ++agents.revision;
    REQUIRE(write_stream_update(stream,
        make_session_poll_request(
            stream.ui, panes, "stream-epoch"), error));
    REQUIRE(read_stream_events(stream, stream_service,
        stream_poll, frame, std::chrono::seconds(3), error));
    INFO(error);
    REQUIRE(frame.kind == SessionStreamServerFrameKind::Events);
    REQUIRE(frame.events);
    REQUIRE(apply_session_poll_response(
        stream.ui, panes, *frame.events, error,
        "stream-epoch"));
    for (size_t index = 0; index < panes.size(); ++index)
    {
        REQUIRE(panes[index].client->projection()
                    .version()
                    .sequence
            == expected_sequences[index]);
    }
    REQUIRE(stream.ui.topology_revision == topology.revision);
    REQUIRE(stream.ui.agent_revision == agents.revision);

    const auto status_started_at
        = std::chrono::steady_clock::now();
    auto status = std::async(std::launch::async, [&] {
        return ControlClient::request(
            control_id, temp.path, "server.status");
    });
    const auto status_deadline
        = status_started_at + std::chrono::seconds(2);
    while (status.wait_for(std::chrono::milliseconds::zero())
            != std::future_status::ready
        && std::chrono::steady_clock::now() < status_deadline)
    {
        control.process_pending(control_handler);
        stream_service.pump(stream_poll);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(status.wait_for(std::chrono::milliseconds::zero())
        == std::future_status::ready);
    REQUIRE(status.get().ok);
    CHECK(std::chrono::steady_clock::now() - status_started_at
        < std::chrono::milliseconds(250));

    REQUIRE(write_stream_update(stream,
        make_session_poll_request(
            stream.ui, panes, "stream-epoch"), error));
    REQUIRE(read_stream_frame(stream, stream_service,
        stream_poll, frame, std::chrono::seconds(1), error));
    INFO(error);
    REQUIRE(frame.kind == SessionStreamServerFrameKind::Heartbeat);
    CHECK(stream_service.connection_count() == 1);
    CHECK(stream.max_queue_bytes == 1024 * 1024);
    CHECK(stream_poll_calls >= 3);
    CHECK(control_requests["session.stream.open"] == 1);
    CHECK(control_requests["server.status"] == 1);
    CHECK(control_requests["session.poll"] == 0);
    CHECK(control_requests["topology.poll"] == 0);
    CHECK(control_requests["agent.poll"] == 0);

    stream.connection->close();
    stream_service.stop();
    control.stop();
}

TEST_CASE("legacy Session control transport load baseline",
    "[.session-load][control][remote-terminal]")
{
    nlohmann::json report = {
        { "fixture", "legacy-per-pane-control" },
        { "latency_unit", "microseconds" },
        { "scenarios", nlohmann::json::array() },
        { "batched_fixture", "session-poll-v1" },
        { "batched_scenarios", nlohmann::json::array() },
    };
    for (const LoadScenario scenario : {
             LoadScenario{ 1, 1 },
             LoadScenario{ 10, 1 },
             LoadScenario{ 50, 2 },
         })
    {
        report["scenarios"].push_back(run_load_scenario(scenario));
        report["batched_scenarios"].push_back(
            run_batched_load_scenario(scenario));
    }

    const std::string encoded = report.dump(2);
    std::cout << "DRAXUL_SESSION_LOAD_BASELINE " << encoded << '\n';
    if (const char* path = std::getenv("DRAXUL_SESSION_LOAD_REPORT");
        path && *path != '\0')
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << encoded << '\n';
        REQUIRE(output.good());
    }
}

TEST_CASE("persistent Session stream load and stalled-reader isolation",
    "[.session-stream-load][session-stream][control][remote-terminal]")
{
    nlohmann::json report = {
        { "fixture", "session-stream-v1" },
        { "scenarios", nlohmann::json::array() },
    };
    for (const LoadScenario scenario : {
             LoadScenario{ 1, 1 },
             LoadScenario{ 10, 1 },
             LoadScenario{ 50, 2 },
         })
    {
        report["scenarios"].push_back(
            run_stream_load_scenario(
                scenario, scenario.panes == 50));
    }
    const std::string encoded = report.dump(2);
    std::cout << "DRAXUL_SESSION_STREAM_LOAD "
              << encoded << '\n';
    if (const char* path
        = std::getenv("DRAXUL_SESSION_STREAM_LOAD_REPORT");
        path && *path != '\0')
    {
        std::ofstream output(
            path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << encoded << '\n';
        REQUIRE(output.good());
    }
}
