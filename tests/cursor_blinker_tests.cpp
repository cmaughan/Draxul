
#include <catch2/catch_all.hpp>

#include <draxul/cursor_blinker.h>

using namespace draxul;

using tp = std::chrono::steady_clock::time_point;

static tp T(int ms_offset)
{
    return tp{} + std::chrono::milliseconds(ms_offset);
}

TEST_CASE("cursor blinker starts visible with no blink timing", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, {});
    INFO("cursor should be visible with no blink timing");
    REQUIRE(b.visible());
    INFO("no deadline when blink is disabled");
    REQUIRE(!b.next_deadline().has_value());
    INFO("advance should not change visibility without a deadline");
    REQUIRE(!b.advance(T(9999)));
}

TEST_CASE("cursor blinker hides cursor when busy", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), true, { 500, 500, 500 });
    INFO("busy should hide cursor");
    REQUIRE(!b.visible());
    INFO("no deadline when busy");
    REQUIRE(!b.next_deadline().has_value());
    INFO("advance should not change visibility when busy");
    REQUIRE(!b.advance(T(9999)));
}

TEST_CASE("cursor blinker does not fire before blinkwait elapses", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 400, 300 });
    INFO("cursor visible before blinkwait");
    REQUIRE(b.visible());
    INFO("deadline set after restart");
    REQUIRE(b.next_deadline().has_value());
    INFO("should not advance before deadline");
    REQUIRE(!b.advance(T(499)));
    INFO("still visible before blinkwait");
    REQUIRE(b.visible());
}

TEST_CASE("cursor blinker hides after blinkwait", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 400, 300 });
    bool changed = b.advance(T(500));
    INFO("advance should return true when visibility changes");
    REQUIRE(changed);
    INFO("cursor should be hidden after blinkwait");
    REQUIRE(!b.visible());
    INFO("next deadline set for blinkon");
    REQUIRE(b.next_deadline().has_value());
}

TEST_CASE("cursor blinker runs full on/off cycle", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 400, 300 });

    b.advance(T(500)); // wait -> off
    INFO("hidden after blinkwait");
    REQUIRE(!b.visible());

    b.advance(T(800)); // off -> on
    INFO("visible after blinkoff");
    REQUIRE(b.visible());

    b.advance(T(1200)); // on -> off
    INFO("hidden after blinkon");
    REQUIRE(!b.visible());

    b.advance(T(1500)); // off -> on
    INFO("visible again after blinkoff");
    REQUIRE(b.visible());
}

TEST_CASE("cursor blinker deadline advances correctly through cycle", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 400, 300 });
    INFO("initial deadline = blinkwait");
    REQUIRE(*b.next_deadline() == T(500));

    b.advance(T(500)); // off; next = T(500) + blinkoff(300)
    INFO("deadline after blinkwait = T(500) + blinkoff");
    REQUIRE(*b.next_deadline() == T(800));

    b.advance(T(800)); // on; next = T(800) + blinkon(400)
    INFO("deadline after blinkoff = T(800) + blinkon");
    REQUIRE(*b.next_deadline() == T(1200));
}

TEST_CASE("cursor blinker restart resets cycle to visible", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 400, 300 });
    b.advance(T(500)); // now hidden

    b.restart(T(600), false, { 500, 400, 300 });
    INFO("restart should make cursor visible again");
    REQUIRE(b.visible());
    INFO("should not fire before new blinkwait");
    REQUIRE(!b.advance(T(1099)));
    INFO("still visible before new blinkwait elapses");
    REQUIRE(b.visible());
}
