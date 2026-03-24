#include <catch2/catch_test_macros.hpp>

#include "gui_action_handler.h"
#include "input_dispatcher.h"
#include "support/fake_window.h"
#include <SDL3/SDL.h>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/host.h>
#include <draxul/ui_panel.h>

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

// ---------------------------------------------------------------------------
// Chord (tmux-style prefix) regression tests
// ---------------------------------------------------------------------------

namespace
{

std::vector<GuiKeybinding> make_chord_bindings()
{
    return {
        // Chord: Ctrl+B (prefix) then | (pipe) -> split_vertical
        { "split_vertical", SDLK_B, kModCtrl, SDLK_BACKSLASH, kModShift },
        // Single-key binding (not a chord)
        { "toggle_diagnostics", 0, kModNone, SDLK_F12, kModNone },
    };
}

} // namespace

TEST_CASE("input dispatcher: chord prefix activates on matching key", "[input_dispatcher][chord]")
{
    auto bindings = make_chord_bindings();
    auto dispatcher = make_test_dispatcher(bindings);

    // Press Ctrl+B (the prefix key)
    KeyEvent prefix_press{ 0, SDLK_B, kModCtrl, true };
    auto action = dispatcher.gui_action_for_key_event(prefix_press);
    // gui_action_for_key_event only checks single-key bindings (skips chords with prefix_key != 0)
    // so it should NOT match here
    REQUIRE_FALSE(action.has_value());
}

TEST_CASE("input dispatcher: non-chord binding still works alongside chord bindings", "[input_dispatcher][chord]")
{
    auto bindings = make_chord_bindings();
    auto dispatcher = make_test_dispatcher(bindings);

    KeyEvent f12{ 0, SDLK_F12, kModNone, true };
    auto action = dispatcher.gui_action_for_key_event(f12);
    REQUIRE(action.has_value());
    REQUIRE(*action == "toggle_diagnostics");
}

TEST_CASE("input dispatcher: gui_action_for_key_event skips chord bindings", "[input_dispatcher][chord]")
{
    // Verify that gui_action_for_key_event correctly ignores bindings with prefix_key != 0
    auto bindings = make_chord_bindings();
    auto dispatcher = make_test_dispatcher(bindings);

    // Press Shift+Backslash (the chord's second key) - should NOT match because
    // gui_action_for_key_event skips chord bindings
    KeyEvent chord_key{ 0, SDLK_BACKSLASH, kModShift, true };
    auto action = dispatcher.gui_action_for_key_event(chord_key);
    REQUIRE_FALSE(action.has_value());
}

// ---------------------------------------------------------------------------
// End-to-end routing tests through connect()
// ---------------------------------------------------------------------------

namespace
{

// Minimal IHost stub that records events for verification.
class StubHost final : public IHost
{
public:
    bool initialize(const HostContext&, IHostCallbacks&) override
    {
        return true;
    }
    void shutdown() override {}
    bool is_running() const override
    {
        return true;
    }
    std::string init_error() const override
    {
        return {};
    }
    void set_viewport(const HostViewport&) override {}
    void on_font_metrics_changed() override {}
    void pump() override {}
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

    void on_key(const KeyEvent& event) override
    {
        key_events.push_back(event);
    }
    void on_text_input(const TextInputEvent& event) override
    {
        text_input_events.push_back(event);
    }
    void on_text_editing(const TextEditingEvent& event) override
    {
        text_editing_events.push_back(event);
    }
    void on_mouse_button(const MouseButtonEvent& event) override
    {
        mouse_button_events.push_back(event);
    }
    void on_mouse_move(const MouseMoveEvent& event) override
    {
        mouse_move_events.push_back(event);
    }
    void on_mouse_wheel(const MouseWheelEvent& event) override
    {
        mouse_wheel_events.push_back(event);
    }

    bool dispatch_action(std::string_view action) override
    {
        dispatched_actions.emplace_back(action);
        return true;
    }

    void request_close() override {}
    Color default_background() const override
    {
        return Color(0.0f, 0.0f, 0.0f, 1.0f);
    }
    HostRuntimeState runtime_state() const override
    {
        return {};
    }
    HostDebugState debug_state() const override
    {
        return {};
    }

    std::vector<KeyEvent> key_events;
    std::vector<TextInputEvent> text_input_events;
    std::vector<TextEditingEvent> text_editing_events;
    std::vector<MouseButtonEvent> mouse_button_events;
    std::vector<MouseMoveEvent> mouse_move_events;
    std::vector<MouseWheelEvent> mouse_wheel_events;
    std::vector<std::string> dispatched_actions;
};

// Test harness that wires a full InputDispatcher through connect().
struct E2ESetup
{
    tests::FakeWindow window;
    UiPanel panel;
    StubHost host;
    std::vector<GuiKeybinding> bindings;
    std::unique_ptr<GuiActionHandler> action_handler;
    std::unique_ptr<InputDispatcher> dispatcher;
    std::string last_executed_action;

    explicit E2ESetup(std::vector<GuiKeybinding> kb = make_test_bindings(), float pixel_scale = 1.0f)
        : bindings(std::move(kb))
    {
        panel.initialize();

        // Build a GuiActionHandler with a UiPanel so that recognized actions
        // (like toggle_diagnostics) execute without crashing.
        GuiActionHandler::Deps ah_deps;
        ah_deps.ui_panel = &panel;
        action_handler = std::make_unique<GuiActionHandler>(std::move(ah_deps));

        InputDispatcher::Deps deps;
        deps.keybindings = &bindings;
        deps.gui_action_handler = action_handler.get();
        deps.ui_panel = &panel;
        deps.host = &host;
        deps.pixel_scale = pixel_scale;
        dispatcher = std::make_unique<InputDispatcher>(std::move(deps));
        dispatcher->connect(window);
    }
};

} // namespace

TEST_CASE("e2e: GUI-bound key is consumed and NOT forwarded to host", "[input_dispatcher][e2e]")
{
    E2ESetup setup;

    // F12 matches toggle_diagnostics -- should be consumed by GuiActionHandler.
    KeyEvent f12{ 0, SDLK_F12, kModNone, true };
    setup.window.on_key(f12);

    REQUIRE(setup.host.key_events.empty());
}

TEST_CASE("e2e: non-GUI key IS forwarded to host", "[input_dispatcher][e2e]")
{
    E2ESetup setup;

    // Plain 'A' has no GUI binding -- should reach the host.
    KeyEvent a_press{ 0, SDLK_A, kModNone, true };
    setup.window.on_key(a_press);

    REQUIRE(setup.host.key_events.size() == 1);
    REQUIRE(setup.host.key_events[0].keycode == SDLK_A);
}

TEST_CASE("e2e: file-drop dispatches open_file action to host", "[input_dispatcher][e2e]")
{
    E2ESetup setup;

    setup.window.on_drop_file("/tmp/test.txt");

    REQUIRE(setup.host.dispatched_actions.size() == 1);
    REQUIRE(setup.host.dispatched_actions[0] == "open_file:/tmp/test.txt");
}

TEST_CASE("e2e: mouse click with pixel_scale=2 produces physical coordinates at host",
    "[input_dispatcher][e2e]")
{
    // pixel_scale = 2.0 (Retina). Logical (100, 50) -> physical (200, 100).
    E2ESetup setup(make_test_bindings(), 2.0f);

    MouseButtonEvent click;
    click.button = 1;
    click.pressed = true;
    click.mod = kModNone;
    click.pos = { 100, 50 };
    setup.window.on_mouse_button(click);

    REQUIRE(setup.host.mouse_button_events.size() == 1);
    REQUIRE(setup.host.mouse_button_events[0].pos.x == 200);
    REQUIRE(setup.host.mouse_button_events[0].pos.y == 100);
}

TEST_CASE("e2e: mouse click with pixel_scale=1 preserves coordinates", "[input_dispatcher][e2e]")
{
    E2ESetup setup(make_test_bindings(), 1.0f);

    MouseButtonEvent click;
    click.button = 1;
    click.pressed = true;
    click.mod = kModNone;
    click.pos = { 75, 120 };
    setup.window.on_mouse_button(click);

    REQUIRE(setup.host.mouse_button_events.size() == 1);
    REQUIRE(setup.host.mouse_button_events[0].pos.x == 75);
    REQUIRE(setup.host.mouse_button_events[0].pos.y == 120);
}

TEST_CASE("e2e: text-editing event is forwarded to host", "[input_dispatcher][e2e]")
{
    E2ESetup setup;

    TextEditingEvent editing;
    editing.text = "compose";
    editing.start = 0;
    editing.length = 3;
    setup.window.on_text_editing(editing);

    REQUIRE(setup.host.text_editing_events.size() == 1);
    REQUIRE(setup.host.text_editing_events[0].text == "compose");
    REQUIRE(setup.host.text_editing_events[0].start == 0);
    REQUIRE(setup.host.text_editing_events[0].length == 3);
}

TEST_CASE("e2e: text-input event is forwarded to host when panel does not want keyboard",
    "[input_dispatcher][e2e]")
{
    E2ESetup setup;

    // Panel is not visible, so wants_keyboard() returns false.
    TextInputEvent input;
    input.text = "hello";
    setup.window.on_text_input(input);

    REQUIRE(setup.host.text_input_events.size() == 1);
    REQUIRE(setup.host.text_input_events[0].text == "hello");
}

TEST_CASE("e2e: mouse wheel with pixel_scale=2 uses physical coordinates",
    "[input_dispatcher][e2e]")
{
    // Non-smooth-scroll path: direct forwarding with scaled coordinates.
    E2ESetup setup(make_test_bindings(), 2.0f);

    MouseWheelEvent wheel;
    wheel.delta = { 0.0f, 3.0f };
    wheel.mod = kModNone;
    wheel.pos = { 40, 60 };
    setup.window.on_mouse_wheel(wheel);

    REQUIRE(setup.host.mouse_wheel_events.size() == 1);
    REQUIRE(setup.host.mouse_wheel_events[0].pos.x == 80);
    REQUIRE(setup.host.mouse_wheel_events[0].pos.y == 120);
}

TEST_CASE("e2e: key-release event is forwarded to host (not consumed by GUI bindings)",
    "[input_dispatcher][e2e]")
{
    E2ESetup setup;

    // Key release for F12 -- GUI bindings only fire on press, so release should forward.
    KeyEvent f12_release{ 0, SDLK_F12, kModNone, false };
    setup.window.on_key(f12_release);

    REQUIRE(setup.host.key_events.size() == 1);
    REQUIRE(setup.host.key_events[0].pressed == false);
}

TEST_CASE("e2e: file-drop with spaces in path is forwarded correctly", "[input_dispatcher][e2e]")
{
    E2ESetup setup;

    setup.window.on_drop_file("/home/user/my documents/file name.txt");

    REQUIRE(setup.host.dispatched_actions.size() == 1);
    REQUIRE(setup.host.dispatched_actions[0] == "open_file:/home/user/my documents/file name.txt");
}
