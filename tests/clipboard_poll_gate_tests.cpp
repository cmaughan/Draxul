#include <catch2/catch_all.hpp>

#include <draxul/clipboard_poll_gate.h>

#include <chrono>

using namespace draxul;
using namespace std::chrono_literals;

TEST_CASE("clipboard poll gate allows the first refresh", "[nvim][clipboard]")
{
    ClipboardPollGate gate(500ms);

    REQUIRE(gate.should_poll(ClipboardPollGate::Clock::time_point{ 1000ms }));
}

TEST_CASE("clipboard poll gate suppresses refreshes before the interval", "[nvim][clipboard]")
{
    ClipboardPollGate gate(500ms);
    const auto start = ClipboardPollGate::Clock::time_point{ 1000ms };

    REQUIRE(gate.should_poll(start));
    REQUIRE_FALSE(gate.should_poll(start + 499ms));
    REQUIRE(gate.should_poll(start + 500ms));
}

TEST_CASE("clipboard poll gate can restart after an eager refresh", "[nvim][clipboard]")
{
    ClipboardPollGate gate(500ms);
    const auto eager_refresh = ClipboardPollGate::Clock::time_point{ 2000ms };

    gate.mark_polled(eager_refresh);

    REQUIRE_FALSE(gate.should_poll(eager_refresh + 250ms));
    REQUIRE(gate.should_poll(eager_refresh + 500ms));
}
