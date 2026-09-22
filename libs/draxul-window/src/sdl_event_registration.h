#pragma once

#include <SDL3/SDL_events.h>

namespace draxul::sdl
{

struct ApplicationEventTypes
{
    Uint32 wake = 0;
    Uint32 file_dialog = 0;

    [[nodiscard]] explicit operator bool() const
    {
        return wake != 0 && file_dialog == wake + 1;
    }
};

using RegisterEvents = Uint32 (*)(int count);

// SDL3 returns zero when it cannot reserve the requested contiguous event IDs.
// Keeping this small adapter injectable lets the failure path be exercised
// without exhausting SDL's process-global event range in a test executable.
[[nodiscard]] ApplicationEventTypes register_application_event_types(
    RegisterEvents register_events);

} // namespace draxul::sdl
