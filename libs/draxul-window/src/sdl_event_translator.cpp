#include "sdl_event_translator.h"

namespace draxul::sdl
{

std::optional<KeyEvent> translate_key(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
        return std::nullopt;
    return KeyEvent{
        (int)event.key.scancode,
        (int)event.key.key,
        static_cast<ModifierFlags>(event.key.mod),
        event.type == SDL_EVENT_KEY_DOWN,
    };
}

std::optional<TextInputEvent> translate_text_input(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_TEXT_INPUT)
        return std::nullopt;
    return TextInputEvent{ event.text.text };
}

std::optional<TextEditingEvent> translate_text_editing(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_TEXT_EDITING)
        return std::nullopt;
    return TextEditingEvent{ event.edit.text, event.edit.start, event.edit.length };
}

std::optional<MouseButtonEvent> translate_mouse_button(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_UP)
        return std::nullopt;
    // SDL3's SDL_MouseButtonEvent does not carry a modifier field. We sample the
    // global keyboard modifier state via SDL_GetModState() as the best available
    // approximation. Under bursty or delayed input this may not reflect the
    // modifier state at event-enqueue time, which is a SDL3 API limitation.
    return MouseButtonEvent{
        (int)event.button.button,
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
        static_cast<ModifierFlags>(SDL_GetModState()),
        (int)event.button.x,
        (int)event.button.y,
    };
}

std::optional<MouseMoveEvent> translate_mouse_move(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_MOUSE_MOTION)
        return std::nullopt;
    // SDL3's SDL_MouseMotionEvent does not carry a modifier field. We sample the
    // global keyboard modifier state via SDL_GetModState() as the best available
    // approximation. Under bursty or delayed input this may not reflect the
    // modifier state at event-enqueue time, which is a SDL3 API limitation.
    return MouseMoveEvent{
        static_cast<ModifierFlags>(SDL_GetModState()),
        (int)event.motion.x,
        (int)event.motion.y,
    };
}

std::optional<MouseWheelEvent> translate_mouse_wheel(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_MOUSE_WHEEL)
        return std::nullopt;
    // SDL3's SDL_MouseWheelEvent does not carry a modifier field. We sample the
    // global keyboard modifier state via SDL_GetModState() as the best available
    // approximation. Under bursty or delayed input this may not reflect the
    // modifier state at event-enqueue time, which is a SDL3 API limitation.
    return MouseWheelEvent{
        event.wheel.x,
        event.wheel.y,
        static_cast<ModifierFlags>(SDL_GetModState()),
        (int)event.wheel.mouse_x,
        (int)event.wheel.mouse_y,
    };
}

std::optional<WindowResizeEvent> translate_resize(SDL_Window* window, const SDL_Event& event)
{
    if (event.type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        return std::nullopt;
    int pw, ph;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    return WindowResizeEvent{ pw, ph };
}

std::optional<DisplayScaleEvent> translate_display_scale(SDL_Window* window, const SDL_Event& event)
{
    if (event.type != SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
        return std::nullopt;
    float scale = SDL_GetWindowDisplayScale(window);
    if (scale <= 0.0f)
        scale = 1.0f;
    return DisplayScaleEvent{ 96.0f * scale };
}

std::optional<std::string> translate_file_drop(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_DROP_FILE || !event.drop.data)
        return std::nullopt;
    return std::string(event.drop.data);
}

} // namespace draxul::sdl
