#pragma once

#include <imgui.h>

namespace draxul
{

// Maps an SDL scancode integer to the corresponding ImGuiKey.
// Returns ImGuiKey_None if the scancode has no ImGui equivalent.
// Takes int rather than SDL_Scancode to avoid leaking SDL headers into callers.
//
// This is the single scancode table for the whole tree: core UI, the renderer
// backends, and every product plugin translate through this one switch.
ImGuiKey sdl_scancode_to_imgui_key(int scancode);

} // namespace draxul
