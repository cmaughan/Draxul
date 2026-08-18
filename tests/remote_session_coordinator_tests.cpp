#include <catch2/catch_test_macros.hpp>

#include "support/temp_dir.h"

#include <draxul/async_frame_stream.h>
#include <draxul/control_plane.h>
#include <draxul/remote_session_coordinator.h>
#include <draxul/remote_session_client.h>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_protocol.h>
#include <draxul/session_protocol.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

namespace
{

TerminalSemanticSnapshot terminal_snapshot(std::string title)
{
    TerminalSemanticSnapshot result{
        .cols = 4,
        .rows = 2,
        .metadata = {
            .cursor = {
                .col = 1,
                .row = 0,
                .visible = true,
            },
            .title = std::move(title),
        },
    };
    result.cells.resize(8);
    result.cells[0].text = "A";
    result.cells[1].text = "B";
    return result;
}

RemoteTerminalAttach terminal_attach(uint64_t sequence,
    std::string server_epoch = "coordinator-epoch",
    std::string title = {})
{
    if (title.empty())
        title = sequence == 0 ? "Initial" : "Updated";
    return {
        .pane = {
            .pane_id = "pane-shared",
            .terminal_id = "terminal-shared",
            .name = "Shared terminal",
            .execution_domain = "server_terminal",
            .process_running = true,
        },
        .state = {
            .kind = RemoteTerminalEventKind::Snapshot,
            .version = {
                .server_epoch = std::move(server_epoch),
                .terminal_id = "terminal-shared",
                .generation = 1,
                .sequence = sequence,
            },
            .controller_client_id = "coordinator-ui",
            .snapshot = terminal_snapshot(std::move(title)),
        },
    };
}

TopologySnapshot session_topology(uint64_t revision)
{
    return {
        .revision = revision,
        .session_id = "default",
        .spaces = {
            {
                .space_id = "space-1",
                .name = "Work",
                .tabs = {
                    {
                        .tab_id = "tab-1",
                        .name = "Tab",
                        .root_node_id = "node-1",
                        .nodes = {
                            {
                                .node_id = "node-1",
                                .is_leaf = true,
                                .pane_id = "pane-shared",
                            },
                        },
                        .panes = {
                            {
                                .pane_id = "pane-shared",
                                .name = "Shared terminal",
                                .domain
                                = TopologyPaneDomain::ServerTerminal,
                                .terminal_id = "terminal-shared",
                            },
                        },
                    },
                },
            },
        },
    };
}

template <typename Predicate>
bool wait_for_condition(Predicate predicate,
    std::chrono::milliseconds timeout = std::chrono::seconds(3))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

std::optional<RemoteTerminalPublishedState> wait_for_state(
    RemoteSessionCoordinator::Registration& registration)
{
    std::optional<RemoteTerminalPublishedState> state;
    wait_for_condition([&] {
        state = registration.take_published_state();
        return state.has_value();
    });
    return state;
}

} // namespace

TEST_CASE("remote Session coordinator owns independent legacy registrations and coalesces wakes",
    "[client][remote-session-coordinator][terminal]")
{
    TempDir temp("draxul-session-coordinator");
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &start_error));

    std::atomic<uint64_t> published_sequence = 0;
    std::atomic<int> attach_calls = 0;
    std::atomic<int> poll_calls = 0;
    std::atomic<int> input_calls = 0;
    std::atomic<int> resize_calls = 0;
    std::atomic<int> take_control_calls = 0;
    std::atomic<int> scrollback_calls = 0;
    std::atomic<int> suspend_calls = 0;
    std::atomic<int> resume_calls = 0;
    std::jthread dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            server.process_pending([&](const ControlRequest& request) {
                if (request.method == "fake.attach")
                {
                    ++attach_calls;
                    return ControlMethodResult::success(
                        remote_terminal_attach_to_json(
                            terminal_attach(published_sequence)));
                }
                if (request.method == "fake.resume")
                {
                    ++resume_calls;
                    return ControlMethodResult::success(
                        remote_terminal_attach_to_json(
                            terminal_attach(published_sequence)));
                }
                if (request.method == "fake.suspend")
                {
                    ++suspend_calls;
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                if (request.method == "fake.poll")
                {
                    ++poll_calls;
                    nlohmann::json events = nlohmann::json::array();
                    const uint64_t after
                        = request.params.value("after_sequence", 0ULL);
                    const uint64_t available = published_sequence;
                    if (after < available)
                    {
                        events.push_back(remote_terminal_event_to_json(
                            terminal_attach(available).state));
                    }
                    return ControlMethodResult::success({
                        { "events", std::move(events) },
                    });
                }
                if (request.method == "fake.input")
                {
                    ++input_calls;
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                if (request.method == "fake.resize")
                {
                    ++resize_calls;
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                if (request.method == "fake.take_control")
                {
                    ++take_control_calls;
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                if (request.method == "fake.scrollback")
                {
                    ++scrollback_calls;
                    return ControlMethodResult::success(
                        remote_terminal_scrollback_page_to_json({
                            .version = {
                                .server_epoch = "coordinator-epoch",
                                .terminal_id = "terminal-shared",
                                .generation = 1,
                                .sequence = published_sequence,
                            },
                            .total_rows = 20,
                            .offset_from_live = request.params.value(
                                "offset_from_live", 0ULL),
                            .cols = 4,
                            .snapshot = terminal_snapshot("History"),
                        }));
                }
                return ControlMethodResult::error(
                    "unknown_method", "Unexpected coordinator method.");
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::atomic<int> wake_calls = 0;
    RemoteSessionCoordinator coordinator({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .expected_server_epoch = "coordinator-epoch",
        .method_prefix = "fake",
        .presentation_suspend_supported = true,
        .wake_consumer = [&] { ++wake_calls; },
    });
    REQUIRE(coordinator.start());
    auto first = coordinator.register_terminal("terminal-shared");
    auto second = coordinator.register_terminal("terminal-shared");
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.id() != second.id());

    REQUIRE(wait_for_condition([&] { return wake_calls.load() == 1; }));
    auto first_initial = wait_for_state(first);
    auto second_initial = wait_for_state(second);
    REQUIRE(first_initial);
    REQUIRE(second_initial);
    CHECK(first_initial->snapshot.metadata.title == "Initial");
    CHECK(second_initial->snapshot.metadata.title == "Initial");
    CHECK(wake_calls == 1);
    REQUIRE(attach_calls == 2);

    // Only one mailbox was acknowledged. The coordinator re-arms exactly one
    // wake for the still-ready registration rather than losing that edge.
    published_sequence = 1;
    REQUIRE(wait_for_condition([&] { return wake_calls.load() == 1; }));
    auto first_updated = wait_for_state(first);
    REQUIRE(first_updated);
    coordinator.acknowledge_wake();
    REQUIRE(wait_for_condition([&] { return wake_calls.load() == 2; }));
    auto second_updated = wait_for_state(second);
    REQUIRE(second_updated);
    CHECK(first_updated->snapshot.metadata.title == "Updated");
    CHECK(second_updated->snapshot.metadata.title == "Updated");
    coordinator.acknowledge_wake();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(wake_calls == 2);

    REQUIRE(first.enqueue_input("abc"));
    REQUIRE(first.enqueue_input_chunks({ "frame-one", "frame-two" }));
    REQUIRE(first.enqueue_resize(80, 24));
    REQUIRE(first.enqueue_take_control());
    REQUIRE(first.enqueue_scroll(3));
    REQUIRE(wait_for_condition([&] {
        return input_calls.load() == 3
            && resize_calls.load() == 1
            && take_control_calls.load() == 1
            && scrollback_calls.load() == 1;
    }));
    auto scrolled = wait_for_state(first);
    REQUIRE(scrolled);
    CHECK(scrolled->scroll_offset == 3);
    REQUIRE(first.enqueue_scroll_to_live());
    auto live = wait_for_state(first);
    REQUIRE(live);
    CHECK(live->scroll_offset == 0);
    coordinator.acknowledge_wake();

    const uint64_t hidden_generation
        = first.set_presentation_visible(false);
    CHECK(hidden_generation == 2);
    REQUIRE(wait_for_condition([&] { return suspend_calls.load() == 1; }));
    published_sequence = 2;
    auto visible_update = wait_for_state(second);
    REQUIRE(visible_update);
    CHECK_FALSE(first.take_published_state().has_value());
    const uint64_t resumed_generation
        = first.set_presentation_visible(true);
    CHECK(resumed_generation == 3);
    REQUIRE(wait_for_condition([&] { return resume_calls.load() == 1; }));
    auto resumed = wait_for_state(first);
    REQUIRE(resumed);
    CHECK(resumed->visibility_generation == resumed_generation);
    CHECK(resumed->snapshot.metadata.title == "Updated");
    coordinator.acknowledge_wake();

    const int polls_before_unregister = poll_calls;
    first.reset();
    CHECK_FALSE(first);
    REQUIRE(second.running());
    REQUIRE(wait_for_condition([&] {
        return poll_calls.load() > polls_before_unregister;
    }));

    second.reset();
    coordinator.stop();
    dispatcher.request_stop();
    dispatcher.join();
    server.stop();
}

TEST_CASE("remote Session coordinator registration teardown is bounded by a blocked legacy request",
    "[client][remote-session-coordinator][shutdown]")
{
    TempDir temp("draxul-session-coordinator-bounded-stop");
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &start_error));

    std::atomic<bool> request_entered = false;
    std::atomic<bool> release_request = false;
    std::jthread dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            server.process_pending([&](const ControlRequest&) {
                request_entered = true;
                while (!release_request && !stop.stop_requested())
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
                }
                return ControlMethodResult::success(
                    remote_terminal_attach_to_json(
                        terminal_attach(0)));
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    RemoteSessionCoordinator coordinator({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .expected_server_epoch = "coordinator-epoch",
    });
    REQUIRE(coordinator.start());
    auto registration
        = coordinator.register_terminal("terminal-shared");
    REQUIRE(registration);
    REQUIRE(wait_for_condition([&] { return request_entered.load(); }));

    const auto started = std::chrono::steady_clock::now();
    registration.reset();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(elapsed < std::chrono::seconds(1));

    release_request = true;
    dispatcher.request_stop();
    dispatcher.join();
    coordinator.stop();
    server.stop();
}

TEST_CASE("remote Session coordinator multiplexes registrations through one Session poll worker",
    "[client][remote-session-coordinator][session-poll]")
{
    TempDir temp("draxul-session-coordinator-batch");
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &start_error));

    auto recovery
        = std::make_shared<ClientRecoveryState>("coordinator-ui");
    REQUIRE(recovery->set_server_epoch("coordinator-epoch"));
    RemoteSessionClient session_client({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .recovery = recovery,
        .externally_fed = true,
    });
    std::atomic<int> session_polls = 0;
    std::atomic<int> legacy_polls = 0;
    std::atomic<int> input_calls = 0;
    std::atomic<uint64_t> published_sequence = 0;
    std::mutex observations_mutex;
    std::vector<SessionPollRequest> observations;
    std::jthread dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            server.process_pending([&](const ControlRequest& control) {
                if (control.method == "session.poll")
                {
                    ++session_polls;
                    std::string error;
                    auto request = session_poll_request_from_json(
                        control.params, error);
                    if (!request)
                    {
                        return ControlMethodResult::error(
                            "invalid_request", error);
                    }
                    {
                        std::lock_guard guard(observations_mutex);
                        observations.push_back(*request);
                    }
                    SessionPollResponse response{
                        .request_serial = request->request_serial,
                        .server_epoch = "coordinator-epoch",
                    };
                    for (const auto& subscription : request->terminals)
                    {
                        SessionTerminalPollBatch batch{
                            .subscription_id
                            = subscription.subscription_id,
                            .terminal_id = subscription.terminal_id,
                            .visibility_generation
                            = subscription.visibility_generation,
                            .suspended = !subscription.visible,
                        };
                        if (!subscription.cursor)
                        {
                            batch.attach = terminal_attach(
                                published_sequence);
                        }
                        else if (subscription.visible
                            && subscription.cursor->after_sequence
                                < published_sequence)
                        {
                            batch.events.push_back(terminal_attach(
                                published_sequence)
                                                       .state);
                        }
                        response.terminals.push_back(std::move(batch));
                    }
                    return ControlMethodResult::success(
                        session_poll_response_to_json(response));
                }
                if (control.method == "fake.poll"
                    || control.method == "fake.attach")
                {
                    ++legacy_polls;
                }
                if (control.method == "fake.input")
                {
                    ++input_calls;
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                return ControlMethodResult::error(
                    "unknown_method", "Unexpected batch test method.");
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    RemoteSessionCoordinator coordinator({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .expected_server_epoch = "coordinator-epoch",
        .method_prefix = "fake",
        .recovery = recovery,
        .session_poll_supported = true,
        .session_client = &session_client,
    });
    REQUIRE(coordinator.start());
    auto first = coordinator.register_terminal("terminal-shared");
    auto second = coordinator.register_terminal("terminal-shared");
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(wait_for_state(first));
    REQUIRE(wait_for_state(second));
    CHECK(legacy_polls == 0);
    REQUIRE(wait_for_condition([&] {
        std::lock_guard guard(observations_mutex);
        if (observations.empty()
            || observations.back().terminals.size() != 2)
        {
            return false;
        }
        return observations.back().terminals[0].subscription_id
            != observations.back().terminals[1].subscription_id;
    }));

    published_sequence = 1;
    auto first_update = wait_for_state(first);
    auto second_update = wait_for_state(second);
    REQUIRE(first_update);
    REQUIRE(second_update);
    CHECK(first_update->snapshot.metadata.title == "Updated");
    CHECK(second_update->snapshot.metadata.title == "Updated");

    REQUIRE(first.enqueue_input("batch-input"));
    REQUIRE(wait_for_condition([&] { return input_calls.load() == 1; }));
    const uint64_t hidden_generation
        = first.set_presentation_visible(false);
    REQUIRE(wait_for_condition([&] {
        std::lock_guard guard(observations_mutex);
        for (const auto& request : observations)
        {
            for (const auto& subscription : request.terminals)
            {
                if (subscription.subscription_id == first.id()
                    && !subscription.visible
                    && subscription.visibility_generation
                        == hidden_generation)
                {
                    return true;
                }
            }
        }
        return false;
    }));
    const uint64_t visible_generation
        = first.set_presentation_visible(true);
    auto resumed = wait_for_state(first);
    REQUIRE(resumed);
    CHECK(resumed->visibility_generation == visible_generation);
    CHECK(legacy_polls == 0);
    CHECK(session_polls > 0);

    coordinator.stop();
    dispatcher.request_stop();
    dispatcher.join();
    server.stop();
}

TEST_CASE("remote Session coordinator prefers one event stream and keeps projection ownership on its worker",
    "[client][remote-session-coordinator][session-stream]")
{
    TempDir temp("draxul-session-coordinator-stream");
    ControlServer control;
    std::string start_error;
    REQUIRE(control.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &start_error));

    AsyncFrameStreamListener stream;
    AsyncFrameStreamError stream_error;
    REQUIRE(stream.start(
        namespaced_control_id("coordinator-stream", temp.path),
        temp.path, stream_error));

    auto recovery
        = std::make_shared<ClientRecoveryState>("coordinator-ui");
    REQUIRE(recovery->set_server_epoch("coordinator-epoch"));
    RemoteSessionClient session_client({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .recovery = recovery,
        .externally_fed = true,
    });

    std::atomic<int> stream_opens = 0;
    std::atomic<int> session_polls = 0;
    std::atomic<int> legacy_polls = 0;
    std::atomic<int> input_calls = 0;
    std::atomic<int> stream_input_commands = 0;
    std::atomic<int> stream_topology_commands = 0;
    std::atomic<int> stream_agent_commands = 0;
    std::atomic<int> stream_scrollback_commands = 0;
    std::mutex observations_mutex;
    std::optional<SessionPollRequest> opened_poll;
    std::vector<SessionPollRequest> updates;
    std::jthread dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            control.process_pending([&](const ControlRequest& request) {
                if (request.method == "session.stream.open")
                {
                    ++stream_opens;
                    std::string error;
                    auto opened = session_stream_open_request_from_json(
                        request.params, error);
                    if (!opened)
                    {
                        return ControlMethodResult::error(
                            "invalid_session_stream", error);
                    }
                    {
                        std::lock_guard guard(observations_mutex);
                        opened_poll = opened->poll;
                    }
                    return ControlMethodResult::success(
                        session_stream_open_response_to_json({
                            .server_epoch = "coordinator-epoch",
                            .endpoint = stream.endpoint(),
                            .ticket = "coordinator-ticket",
                            .heartbeat_interval_ms = 1000,
                            .max_frame_bytes = kControlMaxMessageBytes,
                            .max_queue_bytes
                            = kSessionStreamDefaultQueueBytes,
                        }));
                }
                if (request.method == "session.poll")
                {
                    ++session_polls;
                    return ControlMethodResult::error(
                        "unexpected_poll", "Stream should remain active.");
                }
                if (request.method == "fake.attach"
                    || request.method == "fake.poll")
                {
                    ++legacy_polls;
                }
                if (request.method == "fake.input")
                {
                    ++input_calls;
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                return ControlMethodResult::error(
                    "unknown_method", "Unexpected stream test method.");
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::atomic<bool> stream_connected = false;
    std::atomic<bool> event_sent = false;
    std::jthread stream_peer([&](std::stop_token stop) {
        AsyncFrameStreamError error;
        auto connection = stream.accept(stop, error);
        if (!connection)
            return;
        std::string bytes;
        if (!connection->read_frame(bytes, stop, error))
            return;
        auto value = nlohmann::json::parse(bytes, nullptr, false);
        std::string parse_error;
        auto connect = value.is_discarded()
            ? std::nullopt
            : session_stream_client_frame_from_json(value, parse_error);
        if (!connect
            || connect->kind != SessionStreamClientFrameKind::Connect
            || !connect->connect
            || connect->connect->ticket != "coordinator-ticket")
        {
            return;
        }
        stream_connected = true;
        uint64_t next_server_frame_serial = 1;

        const auto send_events
            = [&](const SessionPollRequest& poll) {
                  SessionPollResponse response{
                      .request_serial = poll.request_serial,
                      .server_epoch = "coordinator-epoch",
                      .topology = {
                          .revision = 7,
                          .snapshot = TopologySnapshot{
                              .revision = 7,
                              .session_id = "default",
                              .spaces = {
                                  {
                                      .space_id = "space-1",
                                      .name = "Work",
                                      .tabs = {
                                          {
                                              .tab_id = "tab-1",
                                              .name = "Tab",
                                              .root_node_id = "node-1",
                                              .nodes = {
                                                  {
                                                      .node_id = "node-1",
                                                      .is_leaf = true,
                                                      .pane_id = "pane-shared",
                                                  },
                                              },
                                              .panes = {
                                                  {
                                                      .pane_id = "pane-shared",
                                                      .name = "Shared terminal",
                                                      .domain = TopologyPaneDomain::ServerTerminal,
                                                      .terminal_id = "terminal-shared",
                                                  },
                                              },
                                          },
                                      },
                                  },
                              },
                          },
                      },
                      .agents = {
                          .revision = 3,
                          .snapshot = ServerAgentSnapshot{
                              .revision = 3,
                              .session_id = "default",
                          },
                      },
                  };
                  for (const auto& subscription : poll.terminals)
                  {
                      response.terminals.push_back({
                          .subscription_id = subscription.subscription_id,
                          .terminal_id = subscription.terminal_id,
                          .visibility_generation
                          = subscription.visibility_generation,
                          .attach = terminal_attach(0),
                      });
                  }
                  const std::string event_bytes
                      = session_stream_server_frame_to_json({
                            .kind = SessionStreamServerFrameKind::Events,
                            .frame_serial = next_server_frame_serial++,
                            .server_epoch = "coordinator-epoch",
                            .events = std::move(response),
                        })
                            .dump();
                  if (!connection->write_frame(
                          event_bytes, stop, error))
                  {
                      return false;
                  }
                  event_sent = true;
                  return true;
              };
        {
            std::lock_guard guard(observations_mutex);
            if (opened_poll && opened_poll->terminals.size() == 2
                && !send_events(*opened_poll))
            {
                return;
            }
        }

        while (!stop.stop_requested())
        {
            bytes.clear();
            if (!connection->read_frame(bytes, stop, error))
                return;
            value = nlohmann::json::parse(bytes, nullptr, false);
            auto frame = value.is_discarded()
                ? std::nullopt
                : session_stream_client_frame_from_json(
                    value, parse_error);
            if (!frame)
            {
                continue;
            }
            if (frame->kind
                    == SessionStreamClientFrameKind::Command
                && frame->command)
            {
                nlohmann::json result = nlohmann::json::object();
                bool command_ok = true;
                std::string command_error_code;
                std::string command_error_message;
                if (frame->command->method == "fake.input")
                {
                    ++stream_input_commands;
                    if (frame->command->params.value(
                            "text", std::string{})
                        == "short-fallback-input")
                    {
                        command_ok = false;
                        command_error_code
                            = "command_result_too_large";
                        command_error_message
                            = "Retry this command over short control.";
                    }
                }
                else if (frame->command->method
                    == "topology.command")
                {
                    ++stream_topology_commands;
                    auto topology = TopologySnapshot{
                        .revision = 8,
                        .session_id = "default",
                        .spaces = {
                            {
                                .space_id = "space-1",
                                .name = "Work",
                                .tabs = {
                                    {
                                        .tab_id = "tab-1",
                                        .name = "Renamed",
                                        .root_node_id = "node-1",
                                        .nodes = { {
                                            .node_id = "node-1",
                                            .is_leaf = true,
                                            .pane_id = "pane-shared",
                                        } },
                                        .panes = { {
                                            .pane_id = "pane-shared",
                                            .name = "Shared terminal",
                                            .domain = TopologyPaneDomain::ServerTerminal,
                                            .terminal_id = "terminal-shared",
                                        } },
                                    },
                                },
                            },
                        },
                    };
                    result = topology_command_result_to_json({
                        .applied = true,
                        .snapshot = std::move(topology),
                    });
                }
                else if (frame->command->method == "agent.restart")
                {
                    ++stream_agent_commands;
                    result["runtime_generation"] = 4;
                }
                else if (frame->command->method == "fake.scrollback")
                {
                    ++stream_scrollback_commands;
                    result = remote_terminal_scrollback_page_to_json({
                        .version = terminal_attach(0).state.version,
                        .total_rows = 5,
                        .offset_from_live = 1,
                        .cols = 4,
                        .snapshot = terminal_snapshot("Scrollback"),
                    });
                }
                else
                {
                    return;
                }
                const std::string response_bytes
                    = session_stream_server_frame_to_json({
                          .kind = SessionStreamServerFrameKind::CommandResult,
                          .frame_serial = next_server_frame_serial++,
                          .server_epoch = "coordinator-epoch",
                          .command_result = SessionStreamCommandResult{
                              .request_id = frame->command->request_id,
                              .ok = command_ok,
                              .result = std::move(result),
                              .error_code
                              = std::move(command_error_code),
                              .error_message
                              = std::move(command_error_message),
                          },
                      })
                          .dump();
                if (!connection->write_frame(
                        response_bytes, stop, error))
                {
                    return;
                }
                continue;
            }
            if (frame->kind != SessionStreamClientFrameKind::Update
                || !frame->update)
            {
                continue;
            }
            const SessionPollRequest poll = frame->update->poll;
            {
                std::lock_guard guard(observations_mutex);
                updates.push_back(poll);
            }
            if (event_sent || poll.terminals.size() != 2)
                continue;
            if (!send_events(poll))
                return;
        }
    });

    RemoteSessionCoordinator coordinator({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .expected_server_epoch = "coordinator-epoch",
        .method_prefix = "fake",
        .recovery = recovery,
        .session_stream_supported = true,
        .session_stream_commands_supported = true,
        .session_poll_supported = true,
        .session_client = &session_client,
    });
    REQUIRE(coordinator.start());
    auto first = coordinator.register_terminal("terminal-shared");
    auto second = coordinator.register_terminal("terminal-shared");
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(wait_for_condition([&] { return stream_connected.load(); }));
    const auto first_state = wait_for_state(first);
    INFO("event sent: " << event_sent.load());
    INFO("stream opens: " << stream_opens.load());
    INFO("Session polls: " << session_polls.load());
    INFO("legacy polls: " << legacy_polls.load());
    INFO("first error: " << first.last_error_code());
    INFO("second error: " << second.last_error_code());
    REQUIRE(first_state);
    REQUIRE(wait_for_state(second));

    REQUIRE(wait_for_condition([&] {
        const auto revisions = session_client.session_poll_revisions();
        return revisions.topology == 7 && revisions.agents == 3;
    }));
    CHECK(stream_opens == 1);
    CHECK(session_polls == 0);
    CHECK(legacy_polls == 0);

    REQUIRE(first.enqueue_input("stream-input"));
    REQUIRE(wait_for_condition(
        [&] { return stream_input_commands.load() == 1; }));
    CHECK(input_calls == 0);
    REQUIRE(first.enqueue_input("short-fallback-input"));
    REQUIRE(wait_for_condition([&] {
        return stream_input_commands.load() == 2
            && input_calls.load() == 1;
    }));

    REQUIRE(session_client.enqueue({
        .command_id = "stream-topology-1",
        .kind = TopologyCommandKind::RenameTab,
        .tab_id = "tab-1",
        .name = "Renamed",
    }));
    bool topology_completed = false;
    REQUIRE(wait_for_condition([&] {
        auto state = session_client.take_published_state();
        if (!state)
            return false;
        topology_completed = std::ranges::any_of(
            state->commands, [](const auto& completion) {
                return completion.ok
                    && completion.command.command_id
                        == "stream-topology-1";
            });
        return topology_completed;
    }));
    CHECK(stream_topology_commands == 1);

    const auto agent_result = coordinator.request_stream_command(
        "agent.restart", {
            { "request_id", "coordinator-ui:agent-1" },
            { "instance_id", "agent-1" },
        });
    REQUIRE(agent_result);
    REQUIRE(agent_result->ok);
    CHECK(agent_result->result.value(
              "runtime_generation", 0ull)
        == 4);
    CHECK(stream_agent_commands == 1);

    REQUIRE(first.enqueue_scroll(1));
    const auto scrolled = wait_for_state(first);
    REQUIRE(scrolled);
    REQUIRE(scrolled->scrollback_page);
    CHECK(scrolled->scroll_offset == 1);
    CHECK(stream_scrollback_commands == 1);

    const uint64_t hidden_generation
        = first.set_presentation_visible(false);
    REQUIRE(wait_for_condition([&] {
        std::lock_guard guard(observations_mutex);
        return std::ranges::any_of(updates,
            [&](const SessionPollRequest& update) {
                return std::ranges::any_of(update.terminals,
                    [&](const SessionTerminalSubscription& subscription) {
                        return subscription.subscription_id == first.id()
                            && !subscription.visible
                            && subscription.visibility_generation
                                == hidden_generation;
                    });
            });
    }));

    const auto stopped_at = std::chrono::steady_clock::now();
    coordinator.stop();
    CHECK(std::chrono::steady_clock::now() - stopped_at
        < std::chrono::seconds(1));
    stream.stop();
    stream_peer.request_stop();
    stream_peer.join();
    dispatcher.request_stop();
    dispatcher.join();
    control.stop();
}

TEST_CASE("remote Session coordinator falls from stream negotiation to Session poll",
    "[client][remote-session-coordinator][session-stream][fallback]")
{
    TempDir temp("draxul-session-stream-poll-fallback");
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &start_error));
    auto recovery
        = std::make_shared<ClientRecoveryState>("coordinator-ui");
    REQUIRE(recovery->set_server_epoch("coordinator-epoch"));
    RemoteSessionClient session_client({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .recovery = recovery,
        .externally_fed = true,
    });
    std::atomic<int> stream_opens = 0;
    std::atomic<int> session_polls = 0;
    std::atomic<int> legacy_attaches = 0;
    std::atomic<int> short_inputs = 0;
    std::jthread dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            server.process_pending([&](const ControlRequest& request) {
                if (request.method == "session.stream.open")
                {
                    ++stream_opens;
                    return ControlMethodResult::error(
                        "stream_unavailable", "Diagnostic rejection.");
                }
                if (request.method == "session.poll")
                {
                    ++session_polls;
                    std::string error;
                    auto poll = session_poll_request_from_json(
                        request.params, error);
                    if (!poll)
                    {
                        return ControlMethodResult::error(
                            "invalid_request", error);
                    }
                    SessionPollResponse response{
                        .request_serial = poll->request_serial,
                        .server_epoch = "coordinator-epoch",
                    };
                    for (const auto& subscription : poll->terminals)
                    {
                        response.terminals.push_back({
                            .subscription_id
                            = subscription.subscription_id,
                            .terminal_id = subscription.terminal_id,
                            .visibility_generation
                            = subscription.visibility_generation,
                            .attach = terminal_attach(0),
                        });
                    }
                    return ControlMethodResult::success(
                        session_poll_response_to_json(response));
                }
                if (request.method == "fake.attach")
                    ++legacy_attaches;
                if (request.method == "fake.input")
                {
                    ++short_inputs;
                    return ControlMethodResult::success(
                        nlohmann::json::object());
                }
                return ControlMethodResult::error(
                    "unknown_method", "Unexpected fallback method.");
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    RemoteSessionCoordinator coordinator({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .expected_server_epoch = "coordinator-epoch",
        .method_prefix = "fake",
        .recovery = recovery,
        .session_stream_supported = true,
        .session_stream_commands_supported = true,
        .session_poll_supported = true,
        .session_client = &session_client,
    });
    REQUIRE(coordinator.start());
    auto registration
        = coordinator.register_terminal("terminal-shared");
    REQUIRE(registration);
    REQUIRE(wait_for_state(registration));
    CHECK(stream_opens == 1);
    CHECK(session_polls > 0);
    CHECK(legacy_attaches == 0);
    const auto transport = coordinator.transport_snapshot();
    CHECK(transport.transport
        == RemoteSessionTransportKind::SessionPoll);
    CHECK(transport.recovery_metrics.fallbacks == 1);
    CHECK(std::ranges::any_of(
        transport.recovery_metrics.reasons,
        [](const ClientRecoveryReasonCount& reason) {
            return reason.kind
                    == ClientRecoveryReasonKind::Fallback
                && reason.channel == "session.stream"
                && reason.reason == "stream_unavailable"
                && reason.count == 1;
        }));
    CHECK(transport.recovery.phase
        == ClientConnectionPhase::Connected);
    REQUIRE(registration.enqueue_input("poll-fallback-input"));
    REQUIRE(wait_for_condition(
        [&] { return short_inputs.load() == 1; }));

    coordinator.stop();
    dispatcher.request_stop();
    dispatcher.join();
    server.stop();
}

TEST_CASE("remote Session coordinator retains projections and recovers quietly through Session poll",
    "[client][remote-session-coordinator][session-stream][recovery]")
{
    TempDir temp("draxul-session-stream-interruption");
    ControlServer control;
    std::string start_error;
    REQUIRE(control.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &start_error));
    AsyncFrameStreamListener stream;
    AsyncFrameStreamError stream_error;
    REQUIRE(stream.start(
        namespaced_control_id("coordinator-interruption", temp.path),
        temp.path, stream_error));

    auto recovery
        = std::make_shared<ClientRecoveryState>("coordinator-ui");
    REQUIRE(recovery->set_server_epoch("coordinator-epoch"));
    RemoteSessionClient session_client({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .recovery = recovery,
        .externally_fed = true,
    });

    std::atomic<int> stream_opens = 0;
    std::atomic<int> session_polls = 0;
    std::atomic<int> legacy_requests = 0;
    std::atomic<bool> allow_poll_recovery = false;
    std::mutex opened_mutex;
    std::optional<SessionPollRequest> opened_poll;
    std::optional<SessionPollRequest> first_fallback_poll;
    std::jthread dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            control.process_pending([&](const ControlRequest& request) {
                if (request.method == "session.stream.open")
                {
                    ++stream_opens;
                    std::string error;
                    auto opened = session_stream_open_request_from_json(
                        request.params, error);
                    if (!opened)
                    {
                        return ControlMethodResult::error(
                            "invalid_session_stream", error);
                    }
                    {
                        std::lock_guard guard(opened_mutex);
                        opened_poll = opened->poll;
                    }
                    return ControlMethodResult::success(
                        session_stream_open_response_to_json({
                            .server_epoch = "coordinator-epoch",
                            .endpoint = stream.endpoint(),
                            .ticket = "interruption-ticket",
                            .heartbeat_interval_ms = 1000,
                            .max_frame_bytes = kControlMaxMessageBytes,
                            .max_queue_bytes
                            = kSessionStreamDefaultQueueBytes,
                        }));
                }
                if (request.method == "session.poll")
                {
                    const int attempt = ++session_polls;
                    std::string error;
                    auto poll = session_poll_request_from_json(
                        request.params, error);
                    if (!poll)
                    {
                        return ControlMethodResult::error(
                            "invalid_request", error);
                    }
                    if (attempt == 1)
                    {
                        std::lock_guard guard(opened_mutex);
                        first_fallback_poll = *poll;
                        return ControlMethodResult::error(
                            "io_error", "Temporary Session poll failure.");
                    }
                    while (!allow_poll_recovery.load()
                        && !stop.stop_requested())
                    {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(1));
                    }
                    SessionPollResponse response{
                        .request_serial = poll->request_serial,
                        .server_epoch = "coordinator-epoch",
                        .topology = {
                            .revision = 8,
                            .snapshot = session_topology(8),
                        },
                        .agents = {
                            .revision = 4,
                            .snapshot = ServerAgentSnapshot{
                                .revision = 4,
                                .session_id = "default",
                            },
                        },
                    };
                    for (const auto& subscription : poll->terminals)
                    {
                        SessionTerminalPollBatch batch{
                            .subscription_id
                            = subscription.subscription_id,
                            .terminal_id = subscription.terminal_id,
                            .visibility_generation
                            = subscription.visibility_generation,
                        };
                        if (subscription.cursor)
                        {
                            batch.events.push_back(
                                terminal_attach(1).state);
                        }
                        else
                        {
                            batch.attach = terminal_attach(1);
                        }
                        response.terminals.push_back(
                            std::move(batch));
                    }
                    return ControlMethodResult::success(
                        session_poll_response_to_json(response));
                }
                if (request.method == "fake.attach"
                    || request.method == "fake.poll")
                {
                    ++legacy_requests;
                }
                return ControlMethodResult::error(
                    "unknown_method",
                    "Unexpected interruption test method.");
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::atomic<bool> stream_connected = false;
    std::atomic<bool> initial_sent = false;
    std::atomic<bool> drop_stream = false;
    std::jthread stream_peer([&](std::stop_token stop) {
        AsyncFrameStreamError error;
        auto connection = stream.accept(stop, error);
        if (!connection)
            return;
        std::string bytes;
        if (!connection->read_frame(bytes, stop, error))
            return;
        std::string parse_error;
        auto connect = session_stream_client_frame_from_json(
            nlohmann::json::parse(bytes, nullptr, false),
            parse_error);
        if (!connect
            || connect->kind != SessionStreamClientFrameKind::Connect
            || !connect->connect
            || connect->connect->ticket != "interruption-ticket")
        {
            return;
        }
        stream_connected = true;

        SessionPollRequest poll;
        {
            std::lock_guard guard(opened_mutex);
            if (opened_poll)
                poll = *opened_poll;
        }
        while (poll.terminals.empty() && !stop.stop_requested())
        {
            bytes.clear();
            if (!connection->read_frame(bytes, stop, error))
                return;
            auto frame = session_stream_client_frame_from_json(
                nlohmann::json::parse(bytes, nullptr, false),
                parse_error);
            if (frame
                && frame->kind
                    == SessionStreamClientFrameKind::Update
                && frame->update)
            {
                poll = frame->update->poll;
            }
        }
        if (stop.stop_requested())
            return;
        SessionPollResponse response{
            .request_serial = poll.request_serial,
            .server_epoch = "coordinator-epoch",
            .topology = {
                .revision = 7,
                .snapshot = session_topology(7),
            },
            .agents = {
                .revision = 3,
                .snapshot = ServerAgentSnapshot{
                    .revision = 3,
                    .session_id = "default",
                },
            },
        };
        for (const auto& subscription : poll.terminals)
        {
            response.terminals.push_back({
                .subscription_id = subscription.subscription_id,
                .terminal_id = subscription.terminal_id,
                .visibility_generation
                = subscription.visibility_generation,
                .attach = terminal_attach(0),
            });
        }
        if (!connection->write_frame(
                session_stream_server_frame_to_json({
                    .kind = SessionStreamServerFrameKind::Events,
                    .frame_serial = 1,
                    .server_epoch = "coordinator-epoch",
                    .events = std::move(response),
                }).dump(),
                stop, error))
        {
            return;
        }
        initial_sent = true;
        while (!drop_stream.load() && !stop.stop_requested())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
        connection->close();
    });

    RemoteSessionCoordinator coordinator({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .expected_server_epoch = "coordinator-epoch",
        .method_prefix = "fake",
        .recovery = recovery,
        .session_stream_supported = true,
        .session_stream_commands_supported = true,
        .session_poll_supported = true,
        .session_client = &session_client,
    });
    REQUIRE(coordinator.start());
    auto registration
        = coordinator.register_terminal("terminal-shared");
    REQUIRE(registration);
    REQUIRE(wait_for_condition([&] {
        return stream_connected.load() && initial_sent.load();
    }));
    const auto terminal_before_outage = wait_for_state(registration);
    REQUIRE(terminal_before_outage);
    CHECK(terminal_before_outage->snapshot.metadata.title
        == "Initial");

    std::optional<TopologySnapshot> topology_before_outage;
    std::optional<ServerAgentSnapshot> agents_before_outage;
    REQUIRE(wait_for_condition([&] {
        auto state = session_client.take_published_state();
        if (state)
        {
            if (state->topology)
                topology_before_outage = std::move(state->topology);
            if (state->agents)
                agents_before_outage = std::move(state->agents);
        }
        return topology_before_outage && agents_before_outage;
    }));
    REQUIRE(topology_before_outage->revision == 7);
    REQUIRE(agents_before_outage->revision == 3);

    drop_stream = true;
    REQUIRE(wait_for_condition([&] {
        return session_polls.load() >= 1
            && coordinator.transport_snapshot().transport
                == RemoteSessionTransportKind::SessionPoll;
    }));
    std::optional<RemoteSessionPublishedState> transient_failure;
    REQUIRE(wait_for_condition([&] {
        auto state = session_client.take_published_state();
        if (state && state->recovery
            && state->recovery->phase
                != ClientConnectionPhase::Connected)
        {
            transient_failure = std::move(state);
        }
        return transient_failure.has_value();
    }));
    CHECK_FALSE(transient_failure->topology_error);
    CHECK_FALSE(transient_failure->agent_error);
    CHECK_FALSE(transient_failure->recovery->sustained_outage);
    CHECK(transient_failure->recovery->interruption_count == 1);
    CHECK(session_client.session_poll_revisions().topology == 7);
    CHECK(session_client.session_poll_revisions().agents == 3);
    {
        std::lock_guard guard(opened_mutex);
        REQUIRE(first_fallback_poll);
        REQUIRE(first_fallback_poll->terminals.size() == 1);
        REQUIRE(first_fallback_poll->terminals.front().cursor);
        CHECK(first_fallback_poll->terminals.front()
                  .cursor->generation
            == 1);
        CHECK(first_fallback_poll->terminals.front()
                  .cursor->after_sequence
            == 0);
    }

    const auto sustained = recovery->snapshot_at("session",
        std::chrono::steady_clock::now()
            + kClientSustainedOutageThreshold
            + std::chrono::milliseconds(1));
    CHECK(sustained.sustained_outage);
    CHECK(sustained.interruption_count == 1);
    CHECK(sustained.current_reason == "io_error");

    const auto interrupted = coordinator.transport_snapshot();
    CHECK(interrupted.transport
        == RemoteSessionTransportKind::SessionPoll);
    CHECK(interrupted.recovery.phase
        != ClientConnectionPhase::Connected);
    CHECK(interrupted.recovery_metrics.fallbacks == 1);
    CHECK(std::ranges::any_of(
        interrupted.recovery_metrics.reasons,
        [](const ClientRecoveryReasonCount& reason) {
            return reason.kind
                    == ClientRecoveryReasonKind::Fallback
                && reason.channel == "session.stream"
                && !reason.reason.empty()
                && reason.count == 1;
        }));
    CHECK(std::ranges::any_of(
        interrupted.recovery_metrics.reasons,
        [](const ClientRecoveryReasonCount& reason) {
            return reason.kind
                    == ClientRecoveryReasonKind::Reconnect
                && reason.channel == "session.poll"
                && reason.reason == "io_error"
                && reason.count == 1;
        }));
    CHECK(legacy_requests == 0);

    allow_poll_recovery = true;
    std::optional<TopologySnapshot> recovered_topology;
    std::optional<ServerAgentSnapshot> recovered_agents;
    std::optional<ClientRecoverySnapshot> recovered_session;
    bool recovery_republished_an_error = false;
    REQUIRE(wait_for_condition([&] {
        auto state = session_client.take_published_state();
        if (state)
        {
            recovery_republished_an_error
                = recovery_republished_an_error
                || state->topology_error.has_value()
                || state->agent_error.has_value();
            if (state->topology)
                recovered_topology = std::move(state->topology);
            if (state->agents)
                recovered_agents = std::move(state->agents);
            if (state->recovery)
                recovered_session = std::move(state->recovery);
        }
        const auto diagnostics = coordinator.transport_snapshot();
        return recovered_topology && recovered_agents
            && recovered_session
            && recovered_session->phase
                == ClientConnectionPhase::Connected
            && diagnostics.recovery.phase
                == ClientConnectionPhase::Connected;
    }));
    REQUIRE(recovered_topology->revision == 8);
    REQUIRE(recovered_agents->revision == 4);
    CHECK_FALSE(recovery_republished_an_error);
    CHECK(recovered_session->interruption_count == 1);
    CHECK(recovered_session->recovery_count == 1);
    const auto recovered_terminal = wait_for_state(registration);
    REQUIRE(recovered_terminal);
    CHECK(recovered_terminal->snapshot.metadata.title == "Updated");
    const auto final_transport = coordinator.transport_snapshot();
    CHECK(final_transport.transport
        == RemoteSessionTransportKind::SessionPoll);
    CHECK(final_transport.recovery_metrics.fallbacks == 1);
    CHECK(legacy_requests == 0);

    coordinator.stop();
    stream.stop();
    stream_peer.request_stop();
    stream_peer.join();
    dispatcher.request_stop();
    dispatcher.join();
    control.stop();
}

TEST_CASE("remote Session coordinator re-handshakes and converges after server epoch replacement",
    "[client][remote-session-coordinator][session-poll][server-restart]")
{
    TempDir temp("draxul-session-coordinator-epoch-replacement");
    const std::string control_id
        = namespaced_control_id(kServerControlId, temp.path);
    auto recovery
        = std::make_shared<ClientRecoveryState>("coordinator-ui");
    REQUIRE(recovery->set_server_identity("epoch-a", "token-a"));
    RemoteSessionClient session_client({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .recovery = recovery,
        .externally_fed = true,
    });

    ControlServer first_server;
    std::string start_error;
    REQUIRE(first_server.start(
        control_id, temp.path, [] {}, &start_error));
    std::atomic<int> first_polls = 0;
    std::jthread first_dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            first_server.process_pending(
                [&](const ControlRequest& request) {
                    if (request.method != "session.poll")
                    {
                        return ControlMethodResult::error(
                            "unknown_method",
                            "Unexpected first-server method.");
                    }
                    ++first_polls;
                    std::string error;
                    auto poll = session_poll_request_from_json(
                        request.params, error);
                    if (!poll)
                    {
                        return ControlMethodResult::error(
                            "invalid_request", error);
                    }
                    SessionPollResponse response{
                        .request_serial = poll->request_serial,
                        .server_epoch = "epoch-a",
                        .topology = {
                            .revision = 7,
                            .snapshot = session_topology(7),
                        },
                        .agents = {
                            .revision = 3,
                            .snapshot = ServerAgentSnapshot{
                                .revision = 3,
                                .session_id = "default",
                            },
                        },
                    };
                    for (const auto& subscription : poll->terminals)
                    {
                        response.terminals.push_back({
                            .subscription_id
                            = subscription.subscription_id,
                            .terminal_id = subscription.terminal_id,
                            .visibility_generation
                            = subscription.visibility_generation,
                            .attach = terminal_attach(0, "epoch-a",
                                "Before replacement"),
                        });
                    }
                    return ControlMethodResult::success(
                        session_poll_response_to_json(response));
                });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::atomic<int> legacy_requests = 0;
    RemoteSessionCoordinator coordinator({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .expected_server_epoch = "epoch-a",
        .method_prefix = "fake",
        .recovery = recovery,
        .session_poll_supported = true,
        .session_client = &session_client,
    });
    REQUIRE(coordinator.start());
    auto registration
        = coordinator.register_terminal("terminal-shared");
    REQUIRE(registration);
    const auto initial_terminal = wait_for_state(registration);
    REQUIRE(initial_terminal);
    CHECK(initial_terminal->snapshot.metadata.title
        == "Before replacement");
    CHECK(initial_terminal->controller_client_id
        == "coordinator-ui");
    CHECK(initial_terminal->process_running);
    REQUIRE(wait_for_condition([&] {
        auto state = session_client.take_published_state();
        return state && state->topology && state->agents
            && state->topology->revision == 7
            && state->agents->revision == 3;
    }));
    REQUIRE(first_polls > 0);

    first_dispatcher.request_stop();
    first_dispatcher.join();
    first_server.stop();

    ControlServer successor;
    REQUIRE(successor.start(
        control_id, temp.path, [] {}, &start_error));
    std::atomic<int> stale_epoch_polls = 0;
    std::atomic<int> refreshed_polls = 0;
    std::atomic<int> hello_requests = 0;
    std::atomic<bool> refreshed_poll_reset_cursors = false;
    std::atomic<bool> refreshed_token_seen = false;
    std::jthread successor_dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            successor.process_pending(
                [&](const ControlRequest& request) {
                    if (request.method == "server.hello")
                    {
                        ++hello_requests;
                        std::string error;
                        const auto hello = server_hello_from_json(
                            request.params, error);
                        if (!hello)
                        {
                            return ControlMethodResult::error(
                                "invalid_hello", error);
                        }
                        return ControlMethodResult::success(
                            server_welcome_to_json({
                                .protocol_major = kServerProtocolMajor,
                                .protocol_minor = kServerProtocolMinor,
                                .server_pid = 42,
                                .server_epoch = "epoch-b",
                                .build_version = "test",
                                .connection_token = "token-b",
                                .capabilities = {
                                    "session-poll-v1",
                                    std::string(
                                        kServerClientTokenCapability),
                                },
                            }));
                    }
                    if (request.method == "session.poll")
                    {
                        std::string error;
                        auto poll = session_poll_request_from_json(
                            request.params, error);
                        if (!poll)
                        {
                            return ControlMethodResult::error(
                                "invalid_request", error);
                        }
                        if (poll->server_epoch != "epoch-b")
                        {
                            ++stale_epoch_polls;
                            return ControlMethodResult::error(
                                "stale_epoch",
                                "The server epoch was replaced.");
                        }
                        ++refreshed_polls;
                        refreshed_token_seen
                            = request.params.value(
                                  "connection_token", std::string{})
                            == "token-b";
                        refreshed_poll_reset_cursors
                            = poll->topology_after_revision == 0
                            && poll->agent_after_revision == 0
                            && poll->terminals.size() == 1
                            && !poll->terminals.front().cursor;
                        SessionPollResponse response{
                            .request_serial = poll->request_serial,
                            .server_epoch = "epoch-b",
                            .topology = {
                                .revision = 1,
                                .snapshot = session_topology(1),
                            },
                            .agents = {
                                .revision = 1,
                                .snapshot = ServerAgentSnapshot{
                                    .revision = 1,
                                    .session_id = "default",
                                },
                            },
                        };
                        for (const auto& subscription : poll->terminals)
                        {
                            response.terminals.push_back({
                                .subscription_id
                                = subscription.subscription_id,
                                .terminal_id = subscription.terminal_id,
                                .visibility_generation
                                = subscription.visibility_generation,
                                .attach = terminal_attach(0, "epoch-b",
                                    "After replacement"),
                            });
                        }
                        return ControlMethodResult::success(
                            session_poll_response_to_json(response));
                    }
                    if (request.method == "fake.attach"
                        || request.method == "fake.poll")
                    {
                        ++legacy_requests;
                    }
                    return ControlMethodResult::error(
                        "unknown_method",
                        "Unexpected successor method.");
                });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::optional<RemoteTerminalPublishedState> replacement_terminal;
    std::optional<TopologySnapshot> replacement_topology;
    std::optional<ServerAgentSnapshot> replacement_agents;
    REQUIRE(wait_for_condition([&] {
        if (auto state = registration.take_published_state())
            replacement_terminal = std::move(state);
        if (auto state = session_client.take_published_state())
        {
            if (state->topology)
                replacement_topology = std::move(state->topology);
            if (state->agents)
                replacement_agents = std::move(state->agents);
        }
        return recovery->server_epoch() == "epoch-b"
            && replacement_terminal
            && replacement_topology && replacement_agents;
    }, std::chrono::seconds(6)));
    REQUIRE(replacement_terminal);
    CHECK(replacement_terminal->snapshot.metadata.title
        == "After replacement");
    CHECK(replacement_terminal->controller_client_id
        == "coordinator-ui");
    CHECK(replacement_terminal->process_running);
    REQUIRE(replacement_topology->revision == 1);
    REQUIRE(replacement_agents->revision == 1);
    CHECK(stale_epoch_polls >= 1);
    CHECK(hello_requests >= 1);
    CHECK(refreshed_polls >= 1);
    CHECK(refreshed_poll_reset_cursors);
    CHECK(refreshed_token_seen);
    CHECK(legacy_requests == 0);
    CHECK(coordinator.transport_snapshot().transport
        == RemoteSessionTransportKind::SessionPoll);

    coordinator.stop();
    successor_dispatcher.request_stop();
    successor_dispatcher.join();
    successor.stop();
}

TEST_CASE("remote Session coordinator falls back when Session poll is unavailable",
    "[client][remote-session-coordinator][session-poll][fallback]")
{
    TempDir temp("draxul-session-coordinator-fallback");
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        namespaced_control_id(kServerControlId, temp.path),
        temp.path, [] {}, &start_error));
    auto recovery
        = std::make_shared<ClientRecoveryState>("coordinator-ui");
    REQUIRE(recovery->set_server_epoch("coordinator-epoch"));
    RemoteSessionClient session_client({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .recovery = recovery,
        .externally_fed = true,
    });
    std::atomic<int> session_polls = 0;
    std::atomic<int> legacy_attaches = 0;
    std::jthread dispatcher([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            server.process_pending([&](const ControlRequest& request) {
                if (request.method == "session.poll")
                {
                    ++session_polls;
                    return ControlMethodResult::error(
                        "unknown_method", "Old server.");
                }
                if (request.method == "fake.attach")
                {
                    ++legacy_attaches;
                    return ControlMethodResult::success(
                        remote_terminal_attach_to_json(
                            terminal_attach(0)));
                }
                if (request.method == "fake.poll")
                {
                    return ControlMethodResult::success({
                        { "events", nlohmann::json::array() },
                    });
                }
                return ControlMethodResult::error(
                    "unknown_method", "Unexpected fallback method.");
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    RemoteSessionCoordinator coordinator({
        .runtime_directory = temp.path,
        .client_id = "coordinator-ui",
        .expected_server_epoch = "coordinator-epoch",
        .method_prefix = "fake",
        .recovery = recovery,
        .session_poll_supported = true,
        .session_client = &session_client,
    });
    REQUIRE(coordinator.start());
    auto registration
        = coordinator.register_terminal("terminal-shared");
    REQUIRE(registration);
    REQUIRE(wait_for_state(registration));
    CHECK(session_polls == 1);
    CHECK(legacy_attaches == 1);
    const auto transport = coordinator.transport_snapshot();
    CHECK(transport.transport
        == RemoteSessionTransportKind::Legacy);
    CHECK(transport.recovery.phase
        == ClientConnectionPhase::Connected);

    coordinator.stop();
    dispatcher.request_stop();
    dispatcher.join();
    server.stop();
}

TEST_CASE("remote Session client accepts multiplexed topology and agent channels",
    "[client][remote-session-client][session-poll]")
{
    auto recovery = std::make_shared<ClientRecoveryState>("session-ui");
    REQUIRE(recovery->set_server_epoch("epoch-a"));
    RemoteSessionClient client({
        .client_id = "session-ui",
        .recovery = recovery,
        .externally_fed = true,
    });
    client.accept_session_poll_topology("epoch-a", {
        .revision = 7,
        .session_id = "default",
    });
    client.accept_session_poll_agents("epoch-a", {
        .revision = 3,
        .session_id = "default",
    });
    const auto revisions = client.session_poll_revisions();
    CHECK(revisions.topology == 7);
    CHECK(revisions.agents == 3);
    auto state = client.take_published_state();
    REQUIRE(state);
    REQUIRE(state->topology);
    REQUIRE(state->agents);
    CHECK(state->topology->revision == 7);
    CHECK(state->agents->revision == 3);
    CHECK(state->topology_server_epoch == "epoch-a");
    CHECK(state->agent_server_epoch == "epoch-a");

    client.accept_session_poll_epoch("epoch-b");
    const auto reset = client.session_poll_revisions();
    CHECK(reset.topology == 0);
    CHECK(reset.agents == 0);
    auto epoch_state = client.take_published_state();
    REQUIRE(epoch_state);
    CHECK(epoch_state->server_epoch_changed);
}
