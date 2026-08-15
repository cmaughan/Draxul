// ScoreView worker/device stress (kanban 16): deterministic interleavings of
// the background engraver, input switching, MIDI flood, and multi-host
// ownership — all through fakes and the device-free feed() seams, never a
// real device. The blocked-worker fixture proves main-thread operations stay
// bounded; the seeded sequence reproduces exactly from the reported seed.

#include <catch2/catch_all.hpp>

#include "support/scoreview_host_fixture.h"

#include <draxul/scoreview/midi_player_input.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{

using draxul::scoreview::DeterministicLayoutEngine;
using draxul::scoreview::FakeEngineState;
using draxul::scoreview::kScoreHostFixtureMinimalScore;
using draxul::scoreview::MidiPlayerInput;
using draxul::scoreview::PlayerInputRig;
using draxul::scoreview::PlayerNoteEvent;
using draxul::scoreview::read_verovio_svg_fixture;
using draxul::scoreview::release_loads;
using draxul::scoreview::ScoreHost;
using draxul::scoreview::ScoreHostTestAccess;
using draxul::scoreview::wait_for_host_install;
using draxul::scoreview::wait_for_loads;
using draxul::scoreview::WindowEngraver;

struct StressHost
{
    std::shared_ptr<FakeEngineState> engine_state = std::make_shared<FakeEngineState>();
    std::shared_ptr<FakeEngineState> worker_state = std::make_shared<FakeEngineState>();
    ScoreHost host;

    bool prime(bool blocked_worker)
    {
        const std::string svg = read_verovio_svg_fixture();
        if (svg.empty())
            return false;
        std::string error;
        if (!ScoreHostTestAccess::prime_window(host,
                std::make_unique<DeterministicLayoutEngine>(engine_state, svg, false),
                kScoreHostFixtureMinimalScore, error))
            return false;
        auto engraver = WindowEngraver::create(
            std::make_unique<DeterministicLayoutEngine>(worker_state, svg, blocked_worker),
            error);
        if (!engraver)
            return false;
        ScoreHostTestAccess::inject_engraver(host, std::move(engraver));
        return true;
    }
};

} // namespace

TEST_CASE("a restyle storm behind a blocked worker stays bounded and converges",
    "[scoreview][stress][engraver]")
{
    StressHost stress;
    REQUIRE(stress.prime(/*blocked_worker=*/true));
    ScoreHostTestAccess::set_transport(stress.host, 1.0, 96.0, true);
    const auto old_strip = ScoreHostTestAccess::strip(stress.host);

    // 25 rapid restyles/restarts while the worker is wedged on job #1. Every
    // main-thread operation must return quickly; generations stay monotonic.
    WindowEngraver::RequestId last_request = 0;
    for (int round = 0; round < 25; ++round)
    {
        const auto start = std::chrono::steady_clock::now();
        if (round % 5 == 4)
            ScoreHostTestAccess::restart(stress.host);
        else
            ScoreHostTestAccess::restyle_current_window(stress.host);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        CHECK(elapsed < std::chrono::milliseconds(100));
        const auto request = ScoreHostTestAccess::pending_request(stress.host);
        REQUIRE(request != 0);
        CHECK(request > last_request);
        last_request = request;
        ScoreHostTestAccess::poll(stress.host); // never installs a stale one
        CHECK(ScoreHostTestAccess::strip(stress.host).get() == old_strip.get());
    }

    // Unwedge everything: only the LATEST generation may install.
    release_loads(stress.worker_state, 1000);
    REQUIRE(wait_for_host_install(stress.host));
    CHECK(ScoreHostTestAccess::pending_request(stress.host) == 0);
    CHECK(ScoreHostTestAccess::strip(stress.host).get() != old_strip.get());
    CHECK(ScoreHostTestAccess::playing(stress.host));
}

TEST_CASE("host destruction under a blocked-then-released worker never hangs",
    "[scoreview][stress][engraver][lifetime]")
{
    // The engraver's destructor joins its worker; a wedged fake must be
    // released before teardown can finish — this drives destruction racing
    // the release from another thread, repeatedly.
    for (int round = 0; round < 5; ++round)
    {
        auto stress = std::make_unique<StressHost>();
        REQUIRE(stress->prime(/*blocked_worker=*/true));
        ScoreHostTestAccess::restyle_current_window(stress->host);
        REQUIRE(wait_for_loads(stress->worker_state, 1));

        std::thread releaser(
            [state = stress->worker_state]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                release_loads(state, 1000);
            });
        stress->host.shutdown(); // joins the worker once the release lands
        releaser.join();
        CHECK_FALSE(stress->host.is_running());
    }
}

TEST_CASE("two hosts own independent engravers and tear down in either order",
    "[scoreview][stress][multi-host]")
{
    for (const bool destroy_first_first : { true, false })
    {
        auto a = std::make_unique<StressHost>();
        auto b = std::make_unique<StressHost>();
        REQUIRE(a->prime(false));
        REQUIRE(b->prime(false));

        ScoreHostTestAccess::set_transport(a->host, 1.0, 90.0, true);
        ScoreHostTestAccess::set_transport(b->host, 2.0, 120.0, false);
        ScoreHostTestAccess::restyle_current_window(a->host);
        ScoreHostTestAccess::restyle_current_window(b->host);
        REQUIRE(wait_for_host_install(a->host));
        REQUIRE(wait_for_host_install(b->host));

        // No cross-talk: each host installed its own generation and kept its
        // own transport.
        CHECK(ScoreHostTestAccess::position_q(a->host) == Catch::Approx(1.0));
        CHECK(ScoreHostTestAccess::position_q(b->host) == Catch::Approx(2.0));
        CHECK(ScoreHostTestAccess::playing(a->host));
        CHECK_FALSE(ScoreHostTestAccess::playing(b->host));

        if (destroy_first_first)
        {
            a.reset();
            b->host.shutdown();
            b.reset();
        }
        else
        {
            b.reset();
            a->host.shutdown();
            a.reset();
        }
    }
}

TEST_CASE("a midi flood from the backend thread stays bounded", "[scoreview][stress][midi]")
{
    MidiPlayerInput midi(MidiPlayerInput::kNoDevice);

    // Two backend threads hammer feed() while the main thread polls slowly —
    // the documented policy: the backlog never exceeds kMaxPendingEvents,
    // overload drops the OLDEST events, fresh input survives.
    std::atomic<bool> stop{ false };
    const auto flood = [&midi, &stop](unsigned char base_pitch) {
        unsigned char message[3] = { 0x90, base_pitch, 100 };
        while (!stop.load(std::memory_order_relaxed))
        {
            for (int i = 0; i < 64; ++i)
            {
                message[1] = static_cast<unsigned char>(base_pitch + (i % 12));
                midi.feed(message, 3);
            }
        }
    };
    std::thread left(flood, 36);
    std::thread right(flood, 72);

    size_t total_polled = 0;
    for (int round = 0; round < 50; ++round)
    {
        std::vector<PlayerNoteEvent> events;
        midi.poll(static_cast<double>(round), events);
        CHECK(events.size() <= MidiPlayerInput::kMaxPendingEvents);
        total_polled += events.size();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stop.store(true);
    left.join();
    right.join();

    // Drain the tail; the queue hands over everything that remains, bounded.
    std::vector<PlayerNoteEvent> tail;
    midi.poll(1000.0, tail);
    CHECK(tail.size() <= MidiPlayerInput::kMaxPendingEvents);
    CHECK(total_polled > 0);

    // The most recent event always survives an overload burst.
    for (size_t i = 0; i < MidiPlayerInput::kMaxPendingEvents + 500; ++i)
    {
        const unsigned char message[3] = { 0x90, 60, 100 };
        midi.feed(message, 3);
    }
    const unsigned char freshest[3] = { 0x90, 108, 100 };
    midi.feed(freshest, 3);
    std::vector<PlayerNoteEvent> drained;
    midi.poll(2000.0, drained);
    REQUIRE_FALSE(drained.empty());
    CHECK(drained.size() <= MidiPlayerInput::kMaxPendingEvents);
    CHECK(drained.back().midi_pitch == 108);
}

TEST_CASE("an input-switch storm keeps the session and lands where it aimed",
    "[scoreview][stress][input]")
{
    StressHost stress;
    REQUIRE(stress.prime(false));
    ScoreHostTestAccess::set_transport(stress.host, 1.25, 104.0, true);
    const double position = ScoreHostTestAccess::position_q(stress.host);
    const double tempo = ScoreHostTestAccess::tempo_qpm(stress.host);

    using GateInput = ScoreHostTestAccess::GateInput;
    for (int round = 0; round < 50; ++round)
    {
        const bool keyboard = (round % 2) == 0;
        ScoreHostTestAccess::select_input(
            stress.host, keyboard ? GateInput::Keyboard : GateInput::Bot);
        CHECK(ScoreHostTestAccess::input_kind(stress.host)
            == (keyboard ? PlayerInputRig::Kind::Keyboard : PlayerInputRig::Kind::Bot));
    }
    CHECK(ScoreHostTestAccess::position_q(stress.host) == Catch::Approx(position));
    CHECK(ScoreHostTestAccess::tempo_qpm(stress.host) == Catch::Approx(tempo));
    CHECK(ScoreHostTestAccess::playing(stress.host));
}

TEST_CASE("a seeded random operation sequence converges and reports its seed",
    "[scoreview][stress][seeded]")
{
    const uint32_t seed = Catch::getSeed();
    CAPTURE(seed); // failures reproduce with --rng-seed <seed>
    std::mt19937 rng(seed);

    StressHost stress;
    REQUIRE(stress.prime(/*blocked_worker=*/true));
    ScoreHostTestAccess::set_transport(stress.host, 0.5, 96.0, true);

    using GateInput = ScoreHostTestAccess::GateInput;
    std::vector<std::string> trace;
    for (int step = 0; step < 60; ++step)
    {
        switch (rng() % 5)
        {
        case 0:
            trace.emplace_back("restyle");
            ScoreHostTestAccess::restyle_current_window(stress.host);
            break;
        case 1:
            trace.emplace_back("restart");
            ScoreHostTestAccess::restart(stress.host);
            break;
        case 2:
            trace.emplace_back("poll");
            ScoreHostTestAccess::poll(stress.host);
            break;
        case 3:
            trace.emplace_back("input-keyboard");
            ScoreHostTestAccess::select_input(stress.host, GateInput::Keyboard);
            break;
        default:
            trace.emplace_back("input-bot");
            ScoreHostTestAccess::select_input(stress.host, GateInput::Bot);
            break;
        }
    }
    CAPTURE(trace);

    // Whatever the sequence was, releasing the worker converges the stream
    // to the newest generation with the transport intact.
    release_loads(stress.worker_state, 100000);
    if (ScoreHostTestAccess::async_pending(stress.host))
        REQUIRE(wait_for_host_install(stress.host));
    CHECK(ScoreHostTestAccess::pending_request(stress.host) == 0);
    CHECK(ScoreHostTestAccess::playing(stress.host));
    stress.host.shutdown();
    CHECK_FALSE(stress.host.is_running());
}
