#include "sdl_clipboard.h"
#include <SDL3/SDL.h>
#include <draxul/perf_timing.h>

namespace draxul::sdl
{

std::string get_clipboard_text()
{
    PERF_MEASURE();
    char* text = SDL_GetClipboardText();
    if (!text)
        return {};
    std::string value = text;
    SDL_free(text);
    return value;
}

bool set_clipboard_text(const std::string& text)
{
    return SDL_SetClipboardText(text.c_str());
}

} // namespace draxul::sdl
