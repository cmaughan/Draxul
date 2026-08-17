#include <catch2/catch_test_macros.hpp>

#include "support/temp_dir.h"

#include <draxul/control_plane.h>
#include <draxul/remote_session_coordinator.h>
#include <draxul/remote_session_client.h>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_protocol.h>
#include <draxul/session_protocol.h>

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

RemoteTerminalAttach terminal_attach(uint64_t sequence)
{
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
                .server_epoch = "coordinator-epoch",
                .terminal_id = "terminal-shared",
                .generation = 1,
                .sequence = sequence,
            },
            .controller_client_id = "coordinator-ui",
            .snapshot = terminal_snapshot(
                sequence == 0 ? "Initial" : "Updated"),
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
