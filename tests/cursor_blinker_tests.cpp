
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

TEST_CASE("cursor blinker default-constructed state is visible, steady", "[cursor]")
{
    CursorBlinker b;
    INFO("default-constructed blinker should be visible");
    REQUIRE(b.visible());
    INFO("no deadline without restart");
    REQUIRE(!b.next_deadline().has_value());
    INFO("advance on default blinker is a no-op");
    REQUIRE(!b.advance(T(0)));
    REQUIRE(!b.advance(T(100000)));
}

TEST_CASE("cursor blinker busy ignores blink timing entirely", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), true, { 500, 500, 500 });
    REQUIRE(!b.visible());
    REQUIRE(!b.next_deadline().has_value());

    // Advance at various times -- should never change state.
    REQUIRE(!b.advance(T(500)));
    REQUIRE(!b.advance(T(1000)));
    REQUIRE(!b.advance(T(100000)));
    REQUIRE(!b.visible());
}

TEST_CASE("cursor blinker zero blinkwait means blink disabled", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 0, 500, 500 });
    INFO("zero blinkwait -> enabled() is false -> steady visible");
    REQUIRE(b.visible());
    REQUIRE(!b.next_deadline().has_value());
    REQUIRE(!b.advance(T(9999)));
    REQUIRE(b.visible());
}

TEST_CASE("cursor blinker zero blinkon means blink disabled", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 0, 500 });
    REQUIRE(b.visible());
    REQUIRE(!b.next_deadline().has_value());
    REQUIRE(!b.advance(T(9999)));
}

TEST_CASE("cursor blinker zero blinkoff means blink disabled", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 500, 0 });
    REQUIRE(b.visible());
    REQUIRE(!b.next_deadline().has_value());
    REQUIRE(!b.advance(T(9999)));
}

TEST_CASE("cursor blinker all-zero timing means blink disabled", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 0, 0, 0 });
    REQUIRE(b.visible());
    REQUIRE(!b.next_deadline().has_value());
}

TEST_CASE("cursor blinker restart from off phase resets to visible", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 400, 300 });
    b.advance(T(500)); // wait -> off
    b.advance(T(800)); // off -> on
    b.advance(T(1200)); // on -> off
    REQUIRE(!b.visible());

    // Restart mid-off-phase.
    b.restart(T(1300), false, { 500, 400, 300 });
    REQUIRE(b.visible());
    REQUIRE(b.next_deadline().has_value());
    REQUIRE(*b.next_deadline() == T(1800)); // 1300 + 500
}

TEST_CASE("cursor blinker restart with blink disabled stops cycle", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 400, 300 });
    b.advance(T(500)); // now hidden, blinking
    REQUIRE(!b.visible());

    // Restart with blink disabled.
    b.restart(T(600), false, { 0, 0, 0 });
    INFO("restart with disabled timing should make visible and steady");
    REQUIRE(b.visible());
    REQUIRE(!b.next_deadline().has_value());
    REQUIRE(!b.advance(T(9999)));
    REQUIRE(b.visible());
}

TEST_CASE("cursor blinker multiple cycles stay periodic", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 100, 200, 150 });

    // Wait -> Off at T(100)
    REQUIRE(b.advance(T(100)));
    REQUIRE(!b.visible());

    // Off -> On at T(250) = 100 + 150
    REQUIRE(b.advance(T(250)));
    REQUIRE(b.visible());

    // On -> Off at T(450) = 250 + 200
    REQUIRE(b.advance(T(450)));
    REQUIRE(!b.visible());

    // Off -> On at T(600) = 450 + 150
    REQUIRE(b.advance(T(600)));
    REQUIRE(b.visible());

    // On -> Off at T(800) = 600 + 200
    REQUIRE(b.advance(T(800)));
    REQUIRE(!b.visible());
}

TEST_CASE("cursor blinker advance returns false when no transition needed", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 400, 300 });

    // Before blinkwait expires.
    REQUIRE(!b.advance(T(0)));
    REQUIRE(!b.advance(T(250)));
    REQUIRE(!b.advance(T(499)));

    // Trigger the transition.
    REQUIRE(b.advance(T(500)));

    // Before blinkoff expires.
    REQUIRE(!b.advance(T(500)));
    REQUIRE(!b.advance(T(799)));
}

TEST_CASE("cursor blinker advance at exact deadline time triggers transition", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 400, 300 });

    // Exactly at blinkwait.
    REQUIRE(b.advance(T(500)));
    REQUIRE(!b.visible());

    // Exactly at blinkoff deadline.
    REQUIRE(b.advance(T(800)));
    REQUIRE(b.visible());

    // Exactly at blinkon deadline.
    REQUIRE(b.advance(T(1200)));
    REQUIRE(!b.visible());
}

TEST_CASE("cursor blinker advance well past deadline still triggers once", "[cursor]")
{
    CursorBlinker b;
    b.restart(T(0), false, { 500, 400, 300 });

    // Jump far past blinkwait -- should transition once.
    bool changed = b.advance(T(5000));
    REQUIRE(changed);
    REQUIRE(!b.visible());

    // The next deadline should be based on T(5000), not the original deadline.
    REQUIRE(*b.next_deadline() == T(5300)); // 5000 + blinkoff(300)
}
