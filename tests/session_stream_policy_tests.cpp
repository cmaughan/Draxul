#include "../libs/draxul-client/src/session_stream_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>

using namespace std::chrono_literals;

namespace draxul
{
namespace
{

using detail::SessionStreamPolicy;

SessionPollRequest poll(uint64_t request_serial = 7)
{
    return {
        .request_serial = request_serial,
        .server_epoch = "epoch-a",
        .topology_after_revision = 4,
        .agent_after_revision = 2,
    };
}

SessionStreamServerFrame heartbeat(
    uint64_t frame_serial, std::string epoch = "epoch-a")
{
    return {
        .kind = SessionStreamServerFrameKind::Heartbeat,
        .frame_serial = frame_serial,
        .server_epoch = std::move(epoch),
    };
}

SessionStreamServerFrame events(uint64_t frame_serial,
    uint64_t request_serial)
{
    return {
        .kind = SessionStreamServerFrameKind::Events,
        .frame_serial = frame_serial,
        .server_epoch = "epoch-a",
        .events = SessionPollResponse{
            .request_serial = request_serial,
            .server_epoch = "epoch-a",
            .topology = {
                .revision = 4,
                .deferred = true,
                .error_code = "deferred",
                .error_message = "No cursor advanced.",
            },
        },
    };
}

SessionStreamCommandResult command_result(uint64_t request_id,
    bool ok, std::string error_code = {})
{
    return {
        .request_id = request_id,
        .ok = ok,
        .error_code = std::move(error_code),
    };
}

SessionStreamPolicy active_policy(
    SessionStreamPolicy::TimePoint now = {})
{
    SessionStreamPolicy policy;
    policy.activate("epoch-a", poll(), 300ms, now);
    return policy;
}

TEST_CASE("Session stream policy admits ordered frames and commits acknowledgements only after writes",
    "[client][session-stream-policy]")
{
    const SessionStreamPolicy::TimePoint now{};
    auto policy = active_policy(now);

    auto accepted = policy.accept_frame(events(1, 7), now + 1ms);
    REQUIRE(accepted.action
        == SessionStreamPolicy::FrameAction::Events);
    REQUIRE(accepted.events);
    CHECK(policy.acknowledgement_pending());

    auto acknowledgement = policy.prepare_update(poll(0));
    REQUIRE(acknowledgement);
    CHECK(acknowledgement->poll.request_serial == 8);

    // Preparing models a write attempt. Until the coordinator reports success,
    // the policy must continue to require an acknowledgement.
    CHECK(policy.acknowledgement_pending());
    policy.commit_update(*acknowledgement);
    CHECK_FALSE(policy.acknowledgement_pending());
    CHECK_FALSE(policy.prepare_update(poll(0)));

    auto next = policy.accept_frame(events(2, 8), now + 2ms);
    CHECK(next.action == SessionStreamPolicy::FrameAction::Events);
    CHECK(policy.acknowledgement_pending());
}

TEST_CASE("Session stream policy rejects stale skipped and duplicate ordering",
    "[client][session-stream-policy]")
{
    const SessionStreamPolicy::TimePoint now{};

    SECTION("stale epoch")
    {
        auto policy = active_policy(now);
        auto decision = policy.accept_frame(
            heartbeat(1, "epoch-b"), now + 1ms);
        CHECK_FALSE(decision);
        CHECK(decision.failure_code == "stale_epoch");
    }

    SECTION("skipped frame")
    {
        auto policy = active_policy(now);
        auto decision = policy.accept_frame(
            heartbeat(2), now + 1ms);
        CHECK_FALSE(decision);
        CHECK(decision.failure_code == "invalid_session_stream");
    }

    SECTION("duplicate frame")
    {
        auto policy = active_policy(now);
        REQUIRE(policy.accept_frame(heartbeat(1), now + 1ms));
        auto decision = policy.accept_frame(
            heartbeat(1), now + 2ms);
        CHECK_FALSE(decision);
        CHECK(decision.failure_code == "invalid_session_stream");
    }

    SECTION("duplicate event request")
    {
        auto policy = active_policy(now);
        REQUIRE(policy.accept_frame(events(1, 7), now + 1ms));
        auto decision = policy.accept_frame(
            events(2, 7), now + 2ms);
        CHECK_FALSE(decision);
        CHECK(decision.failure_code == "invalid_session_stream");
    }
}

TEST_CASE("Session stream policy correlates command results and drops obsolete registrations",
    "[client][session-stream-policy]")
{
    auto policy = active_policy();
    REQUIRE(policy.track_command_write(10, {
        .token = 100,
        .owner = SessionStreamPolicy::CommandOwner::Terminal,
        .registration_id = 5,
    }));
    const std::array<uint64_t, 1> original{ 5 };
    const std::array<uint64_t, 1> replacement{ 6 };
    CHECK_FALSE(policy.accept_command_result(
        command_result(11, true), original));
    CHECK(policy.pending_command_count() == 1);

    auto completed = policy.accept_command_result(
        command_result(10, true), replacement);
    REQUIRE(completed);
    CHECK(completed->command.token == 100);
    CHECK(completed->action
        == SessionStreamPolicy::CommandResultAction::ObsoleteRegistration);
    CHECK(policy.pending_command_count() == 0);
}

TEST_CASE("Session stream policy selects short-control fallback by command owner",
    "[client][session-stream-policy]")
{
    const std::array<uint64_t, 1> active{ 5 };
    for (const auto error_code : {
             "command_result_too_large",
             "unsupported_stream_command",
             "command_unavailable",
         })
    {
        auto policy = active_policy();
        REQUIRE(policy.track_command_write(10, {
            .token = 100,
            .owner = SessionStreamPolicy::CommandOwner::Terminal,
            .registration_id = 5,
        }));
        auto decision = policy.accept_command_result(
            command_result(10, false, error_code), active);
        REQUIRE(decision);
        CHECK(decision->action
            == SessionStreamPolicy::CommandResultAction::RetryOnShortControl);
    }
}

TEST_CASE("Session stream policy retries one topology conflict only after a newer revision",
    "[client][session-stream-policy]")
{
    auto policy = active_policy();
    REQUIRE(policy.track_command_write(10, {
        .token = 100,
        .owner = SessionStreamPolicy::CommandOwner::Topology,
        .topology_revision = 7,
    }));
    REQUIRE(policy.track_command_write(20, {
        .token = 200,
        .owner = SessionStreamPolicy::CommandOwner::Generic,
    }));

    auto conflict = policy.accept_command_result(
        command_result(10, false, "revision_conflict"), {});
    REQUIRE(conflict);
    CHECK(conflict->action
        == SessionStreamPolicy::CommandResultAction::WaitForNewerTopology);
    CHECK(policy.pending_command_count() == 1);
    CHECK(policy.deferred_command_count() == 1);
    CHECK_FALSE(policy.take_topology_retry(7));

    auto retry = policy.take_topology_retry(8);
    REQUIRE(retry);
    CHECK(retry->command.token == 100);
    CHECK(retry->command.retry_count == 1);
    REQUIRE(policy.track_command_write(11, retry->command));

    auto second_conflict = policy.accept_command_result(
        command_result(11, false, "revision_conflict"), {});
    REQUIRE(second_conflict);
    CHECK(second_conflict->action
        == SessionStreamPolicy::CommandResultAction::Complete);
    CHECK(policy.deferred_command_count() == 0);

    // The deferred first command retains its original send position ahead of
    // the still-pending second command.
    auto ordered = active_policy();
    REQUIRE(ordered.track_command_write(10, {
        .token = 100,
        .owner = SessionStreamPolicy::CommandOwner::Topology,
        .topology_revision = 7,
    }));
    REQUIRE(ordered.track_command_write(20, {
        .token = 200,
        .owner = SessionStreamPolicy::CommandOwner::Generic,
    }));
    REQUIRE(ordered.accept_command_result(
        command_result(10, false, "revision_conflict"), {}));
    CHECK(ordered.abandon_sent_commands()
        == std::vector<uint64_t>{ 100, 200 });
}

TEST_CASE("Session stream policy rotates terminals and expires heartbeats at the deadline",
    "[client][session-stream-policy]")
{
    const SessionStreamPolicy::TimePoint now{};
    auto policy = active_policy(now);
    const std::array<uint64_t, 3> registrations{ 11, 22, 33 };

    CHECK(policy.terminal_probe_order(registrations)
        == std::vector<uint64_t>{ 11, 22, 33 });
    policy.advance_terminal_rotation(registrations.size(), 1);
    CHECK(policy.terminal_probe_order(registrations)
        == std::vector<uint64_t>{ 22, 33, 11 });
    CHECK(policy.shared_command_budget(0) == 4);
    CHECK(policy.shared_command_budget(3) == 1);
    CHECK(policy.shared_command_budget(4) == 0);

    CHECK_FALSE(policy.heartbeat_expired(now + 299ms));
    CHECK(policy.heartbeat_time_remaining(now + 299ms) == 1ms);
    CHECK(policy.heartbeat_expired(now + 300ms));
    CHECK(policy.heartbeat_time_remaining(now + 300ms) == 0ms);
}

} // namespace
} // namespace draxul
