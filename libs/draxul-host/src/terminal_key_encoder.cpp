#include <draxul/terminal_key_encoder.h>

#include <SDL3/SDL_keycode.h>

namespace draxul
{

std::string encode_terminal_key(const KeyEvent& event, const VtState& vt)
{
    if ((event.mod & kModCtrl) != 0 && event.keycode >= SDLK_A && event.keycode <= SDLK_Z)
        return std::string(1, static_cast<char>((event.keycode - SDLK_A) + 1));

    const bool alt = (event.mod & kModAlt) != 0;

    switch (event.keycode)
    {
    case SDLK_RETURN:
        return "\r";
    case SDLK_TAB:
        return "\t";
    case SDLK_ESCAPE:
        return "\x1B";
    case SDLK_BACKSPACE:
        return "\x7F";
    case SDLK_UP:
        return vt.cursor_app_mode ? "\x1BOA" : "\x1B[A";
    case SDLK_DOWN:
        return vt.cursor_app_mode ? "\x1BOB" : "\x1B[B";
    case SDLK_RIGHT:
        return vt.cursor_app_mode ? "\x1BOC" : "\x1B[C";
    case SDLK_LEFT:
        return vt.cursor_app_mode ? "\x1BOD" : "\x1B[D";
    case SDLK_HOME:
        return "\x1B[H";
    case SDLK_END:
        return "\x1B[F";
    case SDLK_DELETE:
        return "\x1B[3~";
    case SDLK_INSERT:
        return "\x1B[2~";
    case SDLK_PAGEUP:
        return "\x1B[5~";
    case SDLK_PAGEDOWN:
        return "\x1B[6~";
    case SDLK_F1:
        return "\x1BOP";
    case SDLK_F2:
        return "\x1BOQ";
    case SDLK_F3:
        return "\x1BOR";
    case SDLK_F4:
        return "\x1BOS";
    case SDLK_F5:
        return "\x1B[15~";
    case SDLK_F6:
        return "\x1B[17~";
    case SDLK_F7:
        return "\x1B[18~";
    case SDLK_F8:
        return "\x1B[19~";
    case SDLK_F9:
        return "\x1B[20~";
    case SDLK_F10:
        return "\x1B[21~";
    case SDLK_F11:
        return "\x1B[23~";
    case SDLK_F12:
        return "\x1B[24~";
    default:
        break;
    }

    // Alt+printable-ASCII: prefix the character with ESC.
    // Only applies when a single printable ASCII character would be produced.
    if (alt && event.keycode >= 0x20 && event.keycode <= 0x7E)
    {
        const auto ch = static_cast<char>(event.keycode);
        return std::string("\x1B") + ch;
    }

    return {};
}

} // namespace draxul
