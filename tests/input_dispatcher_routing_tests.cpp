#include <catch2/catch_test_macros.hpp>

#include "input_dispatcher.h"
#include <SDL3/SDL.h>
#include <draxul/app_config.h>
#include <draxul/events.h>

using namespace draxul;

namespace
{

std::vector<GuiKeybinding> make_test_bindings()
{
    return {
        { "copy", 0, kModNone, SDLK_C, kModCtrl | kModShift },
        { "paste", 0, kModNone, SDLK_V, kModCtrl | kModShift },
        { "toggle_diagnostics", 0, kModNone, SDLK_F12, kModNone },
        { "font_increase", 0, kModNone, SDLK_EQUALS, kModCtrl },
    };
}

// Minimal fake deps: no SDL, no UiPanel, no host
// Just keybindings + null everything else
InputDispatcher make_test_dispatcher(const std::vector<GuiKeybinding>& bindings)
{
    InputDispatcher::Deps deps;
    deps.keybindings = &bindings;
    deps.gui_action_handler = nullptr;
    deps.ui_panel = nullptr;
    deps.host = nullptr;
    return InputDispatcher(std::move(deps));
}

} // namespace

TEST_CASE("input dispatcher: GUI action keybinding Ctrl+Shift+C matches copy", "[input_dispatcher]")
{
    auto bindings = make_test_bindings();
    auto dispatcher = make_test_dispatcher(bindings);

    KeyEvent event{ 0, SDLK_C, kModCtrl | kModShift, true };
    auto action = dispatcher.gui_action_for_key_event(event);
    REQUIRE(action.has_value());
    REQUIRE(*action == "copy");
}

TEST_CASE("input dispatcher: GUI action keybinding Ctrl+Shift+V matches paste", "[input_dispatcher]")
{
    auto bindings = make_test_bindings();
    auto dispatcher = make_test_dispatcher(bindings);

    KeyEvent event{ 0, SDLK_V, kModCtrl | kModShift, true };
    auto action = dispatcher.gui_action_for_key_event(event);
    REQUIRE(action.has_value());
    REQUIRE(*action == "paste");
}

TEST_CASE("input dispatcher: F12 with no modifiers matches toggle_diagnostics", "[input_dispatcher]")
{
    auto bindings = make_test_bindings();
    auto dispatcher = make_test_dispatcher(bindings);

    KeyEvent event{ 0, SDLK_F12, kModNone, true };
    auto action = dispatcher.gui_action_for_key_event(event);
    REQUIRE(action.has_value());
    REQUIRE(*action == "toggle_diagnostics");
}

TEST_CASE("input dispatcher: unmapped key returns no GUI action", "[input_dispatcher]")
{
    auto bindings = make_test_bindings();
    auto dispatcher = make_test_dispatcher(bindings);

    KeyEvent event{ 0, SDLK_A, kModNone, true };
    auto action = dispatcher.gui_action_for_key_event(event);
    REQUIRE_FALSE(action.has_value());
}

TEST_CASE("input dispatcher: key with wrong modifiers does not match", "[input_dispatcher]")
{
    auto bindings = make_test_bindings();
    auto dispatcher = make_test_dispatcher(bindings);

    // Ctrl+C alone should not match copy (which requires Ctrl+Shift+C)
    KeyEvent event{ 0, SDLK_C, kModCtrl, true };
    auto action = dispatcher.gui_action_for_key_event(event);
    REQUIRE_FALSE(action.has_value());
}

TEST_CASE("input dispatcher: empty keybindings list returns no action", "[input_dispatcher]")
{
    std::vector<GuiKeybinding> empty;
    auto dispatcher = make_test_dispatcher(empty);

    KeyEvent event{ 0, SDLK_C, kModCtrl | kModShift, true };
    auto action = dispatcher.gui_action_for_key_event(event);
    REQUIRE_FALSE(action.has_value());
}

TEST_CASE("input dispatcher: null keybindings pointer returns no action", "[input_dispatcher]")
{
    InputDispatcher::Deps deps;
    deps.keybindings = nullptr;
    InputDispatcher dispatcher(std::move(deps));

    KeyEvent event{ 0, SDLK_C, kModCtrl | kModShift, true };
    auto action = dispatcher.gui_action_for_key_event(event);
    REQUIRE_FALSE(action.has_value());
}
