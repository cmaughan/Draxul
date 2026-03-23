#pragma once
#include <draxul/input_types.h>
#include <string>

namespace draxul
{

// Event types for window callbacks
struct WindowResizeEvent
{
    int width, height;
};
struct KeyEvent
{
    int scancode;
    int keycode;
    ModifierFlags mod;
    bool pressed;
};
struct TextInputEvent
{
    std::string text;
};
struct TextEditingEvent
{
    std::string text;
    int start = 0;
    int length = 0;
};
struct MouseButtonEvent
{
    int button;
    bool pressed;
    ModifierFlags mod;
    int x, y;
};
struct MouseMoveEvent
{
    ModifierFlags mod;
    int x, y;
};
struct MouseWheelEvent
{
    float dx, dy;
    ModifierFlags mod;
    int x, y;
};
struct DisplayScaleEvent
{
    float display_ppi; // new effective PPI (96 * display_scale)
};

} // namespace draxul
