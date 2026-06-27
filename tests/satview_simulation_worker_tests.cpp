#include "satview_simulation_worker.h"

#include <catch2/catch_test_macros.hpp>
#include <draxul/satview/satview_catalog.h>
#include <thread>

using draxul::satview::SatViewSimulationSnapshot;
using draxul::satview::SatViewSimulationControls;
using draxul::satview::SatViewSimulationWorker;
using draxul::satview::SatViewSnapshotExchange;
using draxul::satview::parse_celestrak_gp_json;

namespace
{

const char* kWorkerTestCatalog = R"json([
  {
    "OBJECT_NAME": "STARLINK-1234",
    "OBJECT_ID": "1958-002B",
    "EPOCH": "2000-06-27T18:50:19.733568",
    "MEAN_MOTION": 10.82419157,
    "ECCENTRICITY": 0.1859667,
    "INCLINATION": 34.2682,
    "RA_OF_ASC_NODE": 348.7242,
    "ARG_OF_PERICENTER": 331.7664,
    "MEAN_ANOMALY": 19.3264,
    "EPHEMERIS_TYPE": 0,
    "CLASSIFICATION_TYPE": "U",
    "NORAD_CAT_ID": 5,
    "ELEMENT_SET_NO": 475,
    "REV_AT_EPOCH": 41366,
    "BSTAR": 0.000028098,
    "MEAN_MOTION_DOT": 0.00000023,
    "MEAN_MOTION_DDOT": 0.0
  }
])json";

template <typename Predicate>
bool wait_for_snapshot(SatViewSimulationWorker& worker, Predicate&& predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto snapshot = worker.acquire_latest();
        if (snapshot && predicate(*snapshot))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

} // namespace

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

TEST_CASE("SatView worker caches tracks until geometry settings change", "[satview][simulation]")
{
    auto parsed = parse_celestrak_gp_json(kWorkerTestCatalog, "test", "test");
    REQUIRE(parsed);

    SatViewSimulationControls controls;
    controls.track_satellite_limit = 1;
    controls.track_sample_count = 12;
    SatViewSimulationWorker worker;
    worker.start(962132419.733568, controls);
    worker.set_catalog(std::move(parsed.catalog), 1);

    std::shared_ptr<const std::vector<draxul::satview::SatelliteOrbitTrack>> initial_tracks;
    REQUIRE(wait_for_snapshot(worker, [&](const SatViewSimulationSnapshot& snapshot) {
        if (!snapshot.tracks || snapshot.tracks->empty())
            return false;
        initial_tracks = snapshot.tracks;
        return initial_tracks->front().teme_points_km.size() == 12;
    }, std::chrono::seconds(2)));

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    auto stable_snapshot = worker.acquire_latest();
    REQUIRE(stable_snapshot);
    CHECK(stable_snapshot->tracks == initial_tracks);

    worker.set_render_settings(1, 24, std::nullopt);
    REQUIRE(wait_for_snapshot(worker, [&](const SatViewSimulationSnapshot& snapshot) {
        return snapshot.tracks && snapshot.tracks != initial_tracks
            && !snapshot.tracks->empty()
            && snapshot.tracks->front().teme_points_km.size() == 24;
    }, std::chrono::seconds(2)));

    constexpr double kResetTime = 1710936000.0;
    worker.set_clock(kResetTime, 1.0f, false);
    REQUIRE(wait_for_snapshot(worker, [&](const SatViewSimulationSnapshot& snapshot) {
        return snapshot.time_speed == 1.0f && !snapshot.paused
            && snapshot.simulation_seconds >= kResetTime
            && snapshot.simulation_seconds < kResetTime + 1.0;
    }, std::chrono::seconds(2)));
}
