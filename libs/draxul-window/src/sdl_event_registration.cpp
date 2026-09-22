#include "sdl_event_registration.h"

namespace draxul::sdl
{

ApplicationEventTypes register_application_event_types(
    RegisterEvents register_events)
{
    if (!register_events)
        return {};

    const Uint32 wake = register_events(2);
    if (wake == 0)
        return {};

    return {
        .wake = wake,
        .file_dialog = wake + 1,
    };
}

} // namespace draxul::sdl
