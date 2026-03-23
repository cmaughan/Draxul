#pragma once
#include <draxul/input_types.h>
#include <glm/glm.hpp>
#include <string>

namespace draxul
{

// Event types for window callbacks
struct WindowResizeEvent
{
    glm::ivec2 size{ 0 };
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
    glm::ivec2 pos{ 0 };
};
struct MouseMoveEvent
{
    ModifierFlags mod;
    glm::ivec2 pos{ 0 };
};
struct MouseWheelEvent
{
    glm::vec2 delta{ 0.0f };
    ModifierFlags mod;
    glm::ivec2 pos{ 0 };
};
struct DisplayScaleEvent
{
    float display_ppi; // new effective PPI (96 * display_scale)
};

} // namespace draxul
