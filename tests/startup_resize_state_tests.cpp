
#include <draxul/startup_resize_state.h>

#include <catch2/catch_all.hpp>

using namespace draxul;

TEST_CASE("startup resize state defers dispatch until the first flush", "[startup]")
{
    StartupResizeState state;
    state.defer(120, 40);

    INFO("a pre-flush resize should stay pending");
    REQUIRE(state.pending());

    auto deferred = state.consume_if_needed(80, 24);
    INFO("the first flush should dispatch a changed pending resize");
    REQUIRE(deferred.has_value());
    INFO("pending columns are preserved");
    REQUIRE(deferred->first == 120);
    INFO("pending rows are preserved");
    REQUIRE(deferred->second == 40);
    INFO("dispatch clears the pending resize");
    REQUIRE(!state.pending());
}

TEST_CASE("startup resize state skips redundant resizes and keeps the latest request", "[startup]")
{
    StartupResizeState state;
    state.defer(100, 30);
    state.defer(140, 50);

    auto redundant = state.consume_if_needed(140, 50);
    INFO("first flush should skip redundant deferred resizes");
    REQUIRE(!redundant.has_value());
    INFO("consuming a redundant resize still clears the pending flag");
    REQUIRE(!state.pending());

    state.defer(150, 55);
    auto latest = state.consume_if_needed(120, 40);
    INFO("a later deferred resize should still dispatch");
    REQUIRE(latest.has_value());
    INFO("latest pending columns win");
    REQUIRE(latest->first == 150);
    INFO("latest pending rows win");
    REQUIRE(latest->second == 55);
}
