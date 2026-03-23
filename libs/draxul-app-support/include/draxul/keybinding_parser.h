#pragma once

#include <cstdint>
#include <draxul/input_types.h>
#include <optional>
#include <string>
#include <string_view>

namespace draxul
{

struct GuiKeybinding;
struct KeyEvent;

std::optional<GuiKeybinding> parse_gui_keybinding(std::string_view action, std::string_view combo);
std::string format_gui_keybinding_combo(int32_t key, ModifierFlags modifiers);
// Returns true if the event matches the *prefix* portion of a chord binding.
// Only meaningful when binding.prefix_key != 0.
bool gui_prefix_matches(const GuiKeybinding& binding, const KeyEvent& event);
bool gui_keybinding_matches(const GuiKeybinding& binding, const KeyEvent& event);

} // namespace draxul
