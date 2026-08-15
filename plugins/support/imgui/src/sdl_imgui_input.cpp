#include <draxul/sdl_imgui_input.h>

#include <SDL3/SDL_scancode.h>

namespace draxul
{

ImGuiKey sdl_scancode_to_imgui_key(int scancode)
{
    switch (scancode)
    {
    case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
    case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
    case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
    case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
    case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
    case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
    case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
    case SDL_SCANCODE_HOME: return ImGuiKey_Home;
    case SDL_SCANCODE_END: return ImGuiKey_End;
    case SDL_SCANCODE_INSERT: return ImGuiKey_Insert;
    case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
    case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
    case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
    case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
    case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDL_SCANCODE_ESCAPE: return ImGuiKey_Escape;
    case SDL_SCANCODE_APOSTROPHE: return ImGuiKey_Apostrophe;
    case SDL_SCANCODE_COMMA: return ImGuiKey_Comma;
    case SDL_SCANCODE_MINUS: return ImGuiKey_Minus;
    case SDL_SCANCODE_PERIOD: return ImGuiKey_Period;
    case SDL_SCANCODE_SLASH: return ImGuiKey_Slash;
    case SDL_SCANCODE_SEMICOLON: return ImGuiKey_Semicolon;
    case SDL_SCANCODE_EQUALS: return ImGuiKey_Equal;
    case SDL_SCANCODE_LEFTBRACKET: return ImGuiKey_LeftBracket;
    case SDL_SCANCODE_BACKSLASH: return ImGuiKey_Backslash;
    case SDL_SCANCODE_RIGHTBRACKET: return ImGuiKey_RightBracket;
    case SDL_SCANCODE_GRAVE: return ImGuiKey_GraveAccent;
    case SDL_SCANCODE_CAPSLOCK: return ImGuiKey_CapsLock;
    case SDL_SCANCODE_SCROLLLOCK: return ImGuiKey_ScrollLock;
    case SDL_SCANCODE_NUMLOCKCLEAR: return ImGuiKey_NumLock;
    case SDL_SCANCODE_PRINTSCREEN: return ImGuiKey_PrintScreen;
    case SDL_SCANCODE_PAUSE: return ImGuiKey_Pause;
    case SDL_SCANCODE_KP_0: return ImGuiKey_Keypad0;
    case SDL_SCANCODE_KP_1: return ImGuiKey_Keypad1;
    case SDL_SCANCODE_KP_2: return ImGuiKey_Keypad2;
    case SDL_SCANCODE_KP_3: return ImGuiKey_Keypad3;
    case SDL_SCANCODE_KP_4: return ImGuiKey_Keypad4;
    case SDL_SCANCODE_KP_5: return ImGuiKey_Keypad5;
    case SDL_SCANCODE_KP_6: return ImGuiKey_Keypad6;
    case SDL_SCANCODE_KP_7: return ImGuiKey_Keypad7;
    case SDL_SCANCODE_KP_8: return ImGuiKey_Keypad8;
    case SDL_SCANCODE_KP_9: return ImGuiKey_Keypad9;
    case SDL_SCANCODE_KP_PERIOD: return ImGuiKey_KeypadDecimal;
    case SDL_SCANCODE_KP_DIVIDE: return ImGuiKey_KeypadDivide;
    case SDL_SCANCODE_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case SDL_SCANCODE_KP_MINUS: return ImGuiKey_KeypadSubtract;
    case SDL_SCANCODE_KP_PLUS: return ImGuiKey_KeypadAdd;
    case SDL_SCANCODE_KP_EQUALS: return ImGuiKey_KeypadEqual;
    case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
    case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
    case SDL_SCANCODE_LALT: return ImGuiKey_LeftAlt;
    case SDL_SCANCODE_LGUI: return ImGuiKey_LeftSuper;
    case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
    case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
    case SDL_SCANCODE_RALT: return ImGuiKey_RightAlt;
    case SDL_SCANCODE_RGUI: return ImGuiKey_RightSuper;
    case SDL_SCANCODE_F1: return ImGuiKey_F1;
    case SDL_SCANCODE_F2: return ImGuiKey_F2;
    case SDL_SCANCODE_F3: return ImGuiKey_F3;
    case SDL_SCANCODE_F4: return ImGuiKey_F4;
    case SDL_SCANCODE_F5: return ImGuiKey_F5;
    case SDL_SCANCODE_F6: return ImGuiKey_F6;
    case SDL_SCANCODE_F7: return ImGuiKey_F7;
    case SDL_SCANCODE_F8: return ImGuiKey_F8;
    case SDL_SCANCODE_F9: return ImGuiKey_F9;
    case SDL_SCANCODE_F10: return ImGuiKey_F10;
    case SDL_SCANCODE_F11: return ImGuiKey_F11;
    case SDL_SCANCODE_F12: return ImGuiKey_F12;
    default:
        if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
            return static_cast<ImGuiKey>(ImGuiKey_A + scancode - SDL_SCANCODE_A);
        if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
            return static_cast<ImGuiKey>(ImGuiKey_1 + scancode - SDL_SCANCODE_1);
        if (scancode == SDL_SCANCODE_0)
            return ImGuiKey_0;
        return ImGuiKey_None;
    }
}

} // namespace draxul
