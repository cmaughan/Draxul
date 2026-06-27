#include "satview_simulation_worker.h"

#include <catch2/catch_test_macros.hpp>

using draxul::satview::SatViewSimulationSnapshot;
using draxul::satview::SatViewSnapshotExchange;

TEST_CASE("SatView snapshot exchange publishes coherent snapshots", "[satview][simulation]")
{
    SatViewSnapshotExchange exchange;

    SatViewSimulationSnapshot first;
    first.generation = 1;
    first.simulation_seconds = 10.0;
    first.status_text = "first";
    REQUIRE(exchange.publish(std::move(first)));

    auto read = exchange.acquire_latest();
    REQUIRE(read);
    CHECK(read->generation == 1);
    CHECK(read->simulation_seconds == 10.0);
    CHECK(read->status_text == "first");

    SatViewSimulationSnapshot second;
    second.generation = 2;
    second.simulation_seconds = 12.0;
    second.status_text = "second";
    REQUIRE(exchange.publish(std::move(second)));

    CHECK(read->generation == 1);
    auto latest = exchange.acquire_latest();
    REQUIRE(latest);
    CHECK(latest->generation == 2);
    CHECK(latest->simulation_seconds == 12.0);
    CHECK(latest->status_text == "second");
}

TEST_CASE("SatView snapshot exchange skips publish when readers hold spare slots", "[satview][simulation]")
{
    SatViewSnapshotExchange exchange;

    SatViewSimulationSnapshot first;
    first.generation = 1;
    REQUIRE(exchange.publish(std::move(first)));
    auto held_first = exchange.acquire_latest();
    REQUIRE(held_first);

    SatViewSimulationSnapshot second;
    second.generation = 2;
    REQUIRE(exchange.publish(std::move(second)));
    auto held_second = exchange.acquire_latest();
    REQUIRE(held_second);

    SatViewSimulationSnapshot third;
    third.generation = 3;
    REQUIRE(exchange.publish(std::move(third)));
    auto held_third = exchange.acquire_latest();
    REQUIRE(held_third);

    SatViewSimulationSnapshot blocked;
    blocked.generation = 4;
    CHECK_FALSE(exchange.publish(std::move(blocked)));
    CHECK(exchange.latest_generation() == 3);
}
