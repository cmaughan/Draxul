#pragma once

#include <draxul/input_types.h>

#include <glm/glm.hpp>

#include <string>

namespace draxul
{

struct KeyEvent
{
    int scancode = 0;
    int keycode = 0;
    ModifierFlags mod = kModNone;
    bool pressed = false;
};

struct TextInputEvent
{
    std::string text;
};

struct MouseButtonEvent
{
    int button = 0;
    bool pressed = false;
    ModifierFlags mod = kModNone;
    glm::ivec2 pos{ 0 };
    int clicks = 1;
};

struct MouseMoveEvent
{
    ModifierFlags mod = kModNone;
    glm::ivec2 pos{ 0 };
    glm::vec2 delta{ 0.0f };
    uint32_t buttons = 0;
};

struct MouseWheelEvent
{
    glm::vec2 delta{ 0.0f };
    ModifierFlags mod = kModNone;
    glm::ivec2 pos{ 0 };
};

} // namespace draxul
