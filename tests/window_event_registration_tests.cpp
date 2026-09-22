#include "../libs/draxul-window/src/sdl_event_registration.h"

#include <catch2/catch_all.hpp>

using namespace draxul;

namespace
{

int requested_event_count = 0;

Uint32 fail_registration(int count)
{
    requested_event_count = count;
    return 0;
}

Uint32 reserve_registration(int count)
{
    requested_event_count = count;
    return SDL_EVENT_USER;
}

} // namespace

TEST_CASE("SDL application event registration rejects allocation failure",
    "[window][sdl]")
{
    requested_event_count = 0;

    const auto events
        = sdl::register_application_event_types(fail_registration);

    CHECK(requested_event_count == 2);
    CHECK_FALSE(events);
    CHECK(events.wake == 0);
    CHECK(events.file_dialog == 0);
}

TEST_CASE("SDL application event registration reserves distinct contiguous IDs",
    "[window][sdl]")
{
    requested_event_count = 0;

    const auto events
        = sdl::register_application_event_types(reserve_registration);

    REQUIRE(events);
    CHECK(requested_event_count == 2);
    CHECK(events.wake == SDL_EVENT_USER);
    CHECK(events.file_dialog == SDL_EVENT_USER + 1);
}
