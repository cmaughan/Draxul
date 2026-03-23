#include <catch2/catch_all.hpp>

#include "frame_timer.h"

using namespace draxul;

TEST_CASE("frame timer: empty state returns zero", "[frame_timer]")
{
    FrameTimer timer;
    REQUIRE(timer.last_ms() == 0.0);
    REQUIRE(timer.average_ms() == 0.0);
}

TEST_CASE("frame timer: single sample", "[frame_timer]")
{
    FrameTimer timer;
    timer.record(16.5);
    REQUIRE(timer.last_ms() == 16.5);
    REQUIRE(timer.average_ms() == 16.5);
}

TEST_CASE("frame timer: average over multiple samples", "[frame_timer]")
{
    FrameTimer timer;
    timer.record(10.0);
    timer.record(20.0);
    timer.record(30.0);
    REQUIRE(timer.last_ms() == 30.0);
    REQUIRE(timer.average_ms() == Catch::Approx(20.0));
}

TEST_CASE("frame timer: ring wraps and only averages last 32", "[frame_timer]")
{
    FrameTimer timer;

    // Fill with 32 samples of 100.0
    for (int i = 0; i < 32; ++i)
        timer.record(100.0);
    REQUIRE(timer.average_ms() == Catch::Approx(100.0));

    // Overwrite all 32 with 50.0
    for (int i = 0; i < 32; ++i)
        timer.record(50.0);
    REQUIRE(timer.last_ms() == 50.0);
    REQUIRE(timer.average_ms() == Catch::Approx(50.0));
}

TEST_CASE("frame timer: partial ring wrap averages correctly", "[frame_timer]")
{
    FrameTimer timer;

    // Fill ring with 32 values of 10.0
    for (int i = 0; i < 32; ++i)
        timer.record(10.0);

    // Overwrite half with 30.0
    for (int i = 0; i < 16; ++i)
        timer.record(30.0);

    // Average should be (16 * 10.0 + 16 * 30.0) / 32 = 20.0
    REQUIRE(timer.average_ms() == Catch::Approx(20.0));
}
