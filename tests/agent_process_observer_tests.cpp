#include <catch2/catch_all.hpp>

#include "agent_observation_schedule.h"
#include "agent_process_observer.h"
#include "support/test_support.h"

#include <atomic>
#include <chrono>
#include <cstdint>

using namespace draxul;

TEST_CASE("agent observation scheduling handles debounce and reconciliation deterministically",
    "[agent_process_observer]")
{
    using Schedule = detail::AgentObservationSchedule;
    using namespace std::chrono_literals;
    const auto start = Schedule::Clock::time_point{};
    Schedule schedule(start, 0, {
                                    .change_debounce = 10ms,
                                    .present_reconcile = 20ms,
                                    .missing_reconcile = 30ms,
                                });

    CHECK(schedule.due(start));
    schedule.complete(start, false, 0);
    CHECK(schedule.next_deadline() == start + 30ms);

    schedule.note_activity(start + 5ms, 1);
    CHECK_FALSE(schedule.due(start + 14ms));
    schedule.note_activity(start + 10ms, 2);
    CHECK(schedule.due(start + 15ms));

    schedule.complete(start + 15ms, true, 2);
    CHECK(schedule.agent_present());
    schedule.note_activity(start + 16ms, 3);
    CHECK_FALSE(schedule.due(start + 34ms));
    CHECK(schedule.due(start + 35ms));

    schedule.complete(start + 35ms, false, 3);
    CHECK_FALSE(schedule.agent_present());
    CHECK(schedule.next_deadline() == start + 65ms);
}

TEST_CASE("agent process observer publishes in probe order and stop resets state",
    "[agent_process_observer]")
{
    using namespace std::chrono_literals;
    std::atomic<uint64_t> probe_count = 0;

    auto probe = [&]() -> std::optional<AgentProcessObservation> {
        const uint64_t sequence = ++probe_count;
        AgentProcessObservation observation;
        observation.captured_at = std::chrono::steady_clock::now();
        observation.processes.push_back({
            .process_id = sequence,
            .executable = "ordinary-shell",
        });
        return observation;
    };
    detail::AgentProcessObserver observer(probe, {}, {}, {}, {
                                                                 .intervals = {
                                                                     .change_debounce = 0ms,
                                                                     .present_reconcile = 1h,
                                                                     .missing_reconcile = 1h,
                                                                 },
                                                                 .poll_ceiling = 1h,
                                                             });

    observer.start();
    REQUIRE(draxul::tests::wait_until(
        [&] {
            const auto latest = observer.latest();
            return latest && latest->processes[0].process_id == 1;
        },
        1s));
    REQUIRE(observer.latest());
    CHECK(observer.latest()->processes[0].process_id == 1);

    // With no agent present, output activity must wake the observer's default
    // wait immediately instead of waiting for the long reconciliation timeout.
    observer.note_activity();
    REQUIRE(draxul::tests::wait_until(
        [&] {
            const auto latest = observer.latest();
            return latest && latest->processes[0].process_id == 2;
        },
        1s));
    REQUIRE(observer.latest());
    CHECK(observer.latest()->processes[0].process_id == 2);

    observer.stop();
    CHECK_FALSE(observer.latest());
    const uint64_t stopped_probe_count = probe_count.load();
    observer.start();
    CHECK(probe_count.load() == stopped_probe_count);
    CHECK_FALSE(observer.latest());
}
