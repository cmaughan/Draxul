#include <catch2/catch_test_macros.hpp>

#include "gui_action_handler.h"
#include "input_dispatcher.h"
#include "support/fake_host.h"
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

std::vector<GuiKeybinding> make_ctrl_c_copy_bindings()
{
    return {
        { "copy", 0, kModNone, SDLK_C, kModCtrl },
    };
}

class TestInputRouter final : public IInputRouter
{
public:
    std::function<IHost*()> overlay_host_fn;
    std::function<void(int)> activate_pane_fn;

    IHost* overlay_host() override
    {
        return overlay_host_fn ? overlay_host_fn() : nullptr;
    }

    HostManager* host_manager() override
    {
        return nullptr;
    }

    int hit_test_tab(int, int) override
    {
        return 0;
    }

    LeafId hit_test_pane_pill(int, int) override
    {
        return kInvalidLeaf;
    }

    int tab_bar_height_phys() override
    {
        return 0;
    }

    std::pair<int, int> cell_size_phys() override
    {
        return { 0, 0 };
    }

    void activate_tab(int) override {}

    void activate_pane(int one_based_index) override
    {
        if (activate_pane_fn)
            activate_pane_fn(one_based_index);
    }

    void begin_tab_rename(int) override {}
    void begin_pane_rename(LeafId) override {}

    bool is_editing() override
    {
        return false;
    }

    bool rename_text_input(const std::string&) override
    {
        return false;
    }

    bool rename_key(int) override
    {
        return false;
    }

    void commit_rename() override {}
};

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

// Minimal IHost stub — uses shared FakeHost which records every input event.
using StubHost = draxul::tests::FakeHost;

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
        ah_deps.focused_host = [this]() -> IHost* { return &host; };
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

TEST_CASE("e2e: Ctrl+C GUI copy suppresses the follow-up text input event",
    "[input_dispatcher][e2e]")
{
    E2ESetup setup(make_ctrl_c_copy_bindings());

    setup.window.on_key(KeyEvent{ 0, SDLK_C, kModCtrl, true });

    REQUIRE(setup.host.dispatched_actions.size() == 1);
    REQUIRE(setup.host.dispatched_actions[0] == "copy");
    REQUIRE(setup.host.key_events.empty());

    TextInputEvent input;
    input.text = std::string(1, '\x03');
    setup.window.on_text_input(input);

    REQUIRE(setup.host.text_input_events.empty());
}

TEST_CASE("e2e: stale GUI text suppression is cleared before later normal typing",
    "[input_dispatcher][e2e]")
{
    E2ESetup setup(make_ctrl_c_copy_bindings());

    setup.window.on_key(KeyEvent{ 0, SDLK_C, kModCtrl, true });

    REQUIRE(setup.host.dispatched_actions.size() == 1);
    REQUIRE(setup.host.dispatched_actions[0] == "copy");

    setup.window.on_key(KeyEvent{ 0, SDLK_A, kModNone, true });

    TextInputEvent input;
    input.text = "a";
    setup.window.on_text_input(input);

    REQUIRE(setup.host.key_events.size() == 1);
    REQUIRE(setup.host.key_events[0].keycode == SDLK_A);
    REQUIRE(setup.host.text_input_events.size() == 1);
    REQUIRE(setup.host.text_input_events[0].text == "a");
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

// ---------------------------------------------------------------------------
// Overlay-host routing (regression guard for WI 50)
// ---------------------------------------------------------------------------

namespace
{

// Same as E2ESetup but wires an overlay_host getter that flips between an
// active overlay (returns &overlay) and inactive (returns null) so individual
// tests can exercise both states.
struct OverlayE2ESetup
{
    tests::FakeWindow window;
    UiPanel panel;
    StubHost host;
    StubHost overlay;
    TestInputRouter router;
    bool overlay_active = true;
    std::vector<GuiKeybinding> bindings;
    std::unique_ptr<GuiActionHandler> action_handler;
    std::unique_ptr<InputDispatcher> dispatcher;

    OverlayE2ESetup()
        : bindings(make_test_bindings())
    {
        panel.initialize();

        GuiActionHandler::Deps ah_deps;
        ah_deps.ui_panel = &panel;
        action_handler = std::make_unique<GuiActionHandler>(std::move(ah_deps));

        InputDispatcher::Deps deps;
        deps.keybindings = &bindings;
        deps.gui_action_handler = action_handler.get();
        deps.ui_panel = &panel;
        deps.host = &host;
        deps.pixel_scale = 1.0f;
        router.overlay_host_fn = [this]() -> IHost* {
            return overlay_active ? &overlay : nullptr;
        };
        deps.router = &router;
        dispatcher = std::make_unique<InputDispatcher>(std::move(deps));
        dispatcher->connect(window);
    }
};

MouseButtonEvent make_click(int x, int y)
{
    MouseButtonEvent ev;
    ev.button = 1;
    ev.pressed = true;
    ev.mod = kModNone;
    ev.pos = { x, y };
    return ev;
}

MouseWheelEvent make_wheel(int x, int y)
{
    MouseWheelEvent ev;
    ev.delta = { 0.0f, 1.0f };
    ev.mod = kModNone;
    ev.pos = { x, y };
    return ev;
}

MouseMoveEvent make_move(int x, int y)
{
    MouseMoveEvent ev;
    ev.pos = { x, y };
    ev.delta = { 0.0f, 0.0f };
    return ev;
}

} // namespace

TEST_CASE("overlay: mouse button is NOT forwarded to underlying host", "[input_dispatcher][overlay]")
{
    OverlayE2ESetup setup;
    setup.window.on_mouse_button(make_click(10, 20));
    REQUIRE(setup.host.mouse_button_events.empty());
}

TEST_CASE("overlay: mouse wheel is NOT forwarded to underlying host", "[input_dispatcher][overlay]")
{
    OverlayE2ESetup setup;
    setup.window.on_mouse_wheel(make_wheel(10, 20));
    REQUIRE(setup.host.mouse_wheel_events.empty());
}

TEST_CASE("overlay: mouse move is NOT forwarded to underlying host", "[input_dispatcher][overlay]")
{
    OverlayE2ESetup setup;
    setup.window.on_mouse_move(make_move(10, 20));
    REQUIRE(setup.host.mouse_move_events.empty());
}

TEST_CASE("overlay: text-editing is NOT forwarded to underlying host", "[input_dispatcher][overlay]")
{
    OverlayE2ESetup setup;
    TextEditingEvent ev;
    ev.text = "compose";
    ev.start = 0;
    ev.length = 3;
    setup.window.on_text_editing(ev);
    REQUIRE(setup.host.text_editing_events.empty());
}

TEST_CASE("overlay: text input goes to overlay host, not underlying", "[input_dispatcher][overlay]")
{
    OverlayE2ESetup setup;
    TextInputEvent ev;
    ev.text = "x";
    setup.window.on_text_input(ev);
    REQUIRE(setup.host.text_input_events.empty());
    REQUIRE(setup.overlay.text_input_events.size() == 1);
    REQUIRE(setup.overlay.text_input_events[0].text == "x");
}

TEST_CASE("overlay: key event goes to overlay host, not underlying", "[input_dispatcher][overlay]")
{
    OverlayE2ESetup setup;
    KeyEvent ev{ 0, SDLK_A, kModNone, true };
    setup.window.on_key(ev);
    REQUIRE(setup.host.key_events.empty());
    REQUIRE(setup.overlay.key_events.size() == 1);
    REQUIRE(setup.overlay.key_events[0].keycode == SDLK_A);
}

TEST_CASE("overlay: when cleared, all event types reach the underlying host again",
    "[input_dispatcher][overlay]")
{
    OverlayE2ESetup setup;
    setup.overlay_active = false;

    setup.window.on_mouse_button(make_click(10, 20));
    setup.window.on_mouse_wheel(make_wheel(10, 20));
    setup.window.on_mouse_move(make_move(10, 20));

    TextEditingEvent te;
    te.text = "compose";
    te.start = 0;
    te.length = 3;
    setup.window.on_text_editing(te);

    TextInputEvent ti;
    ti.text = "y";
    setup.window.on_text_input(ti);

    KeyEvent ke{ 0, SDLK_B, kModNone, true };
    setup.window.on_key(ke);

    REQUIRE(setup.host.mouse_button_events.size() == 1);
    REQUIRE(setup.host.mouse_wheel_events.size() == 1);
    REQUIRE(setup.host.mouse_move_events.size() == 1);
    REQUIRE(setup.host.text_editing_events.size() == 1);
    REQUIRE(setup.host.text_input_events.size() == 1);
    REQUIRE(setup.host.key_events.size() == 1);

    // And the overlay sees nothing while inactive.
    REQUIRE(setup.overlay.mouse_button_events.empty());
    REQUIRE(setup.overlay.text_input_events.empty());
    REQUIRE(setup.overlay.key_events.empty());
}

// ---------------------------------------------------------------------------
// Chord state-machine end-to-end tests (WI 29 — regression guard for the
// tmux-style prefix path in InputDispatcher::on_key_event).
// ---------------------------------------------------------------------------
//
// The chord state machine supports a timeout-backed visual indicator and a
// pane-select submode (`prefix, 0, 1..9`) on top of the existing two-step
// tmux-style prefix flow. These tests pin down the surprising edges
// (modifier-during-prefix, prefix-then-unrecognised-key, prefix-then-prefix-
// again, focus-change-during-prefix) so regressions are caught at the routing
// layer rather than via QA.

namespace
{

struct ChordE2ESetup
{
    tests::FakeWindow window;
    UiPanel panel;
    StubHost host;
    StubHost other_host;
    TestInputRouter router;
    std::vector<GuiKeybinding> bindings;
    std::unique_ptr<GuiActionHandler> action_handler;
    std::unique_ptr<InputDispatcher> dispatcher;
    int split_vertical_calls = 0;
    std::vector<int> activate_pane_calls;

    ChordE2ESetup()
    {
        // Chord: Ctrl+B (prefix) then Shift+\ (a.k.a. |) -> split_vertical.
        // Plus a single-key F12 binding for sanity.
        bindings = {
            { "split_vertical", SDLK_B, kModCtrl, SDLK_BACKSLASH, kModShift },
            { "toggle_diagnostics", 0, kModNone, SDLK_F12, kModNone },
        };

        panel.initialize();

        GuiActionHandler::Deps ah_deps;
        ah_deps.ui_panel = &panel;
        ah_deps.on_split_vertical = [this](std::optional<HostKind>) { ++split_vertical_calls; };
        action_handler = std::make_unique<GuiActionHandler>(std::move(ah_deps));

        InputDispatcher::Deps deps;
        deps.keybindings = &bindings;
        deps.gui_action_handler = action_handler.get();
        deps.ui_panel = &panel;
        deps.host = &host;
        deps.pixel_scale = 1.0f;
        router.activate_pane_fn = [this](int index) { activate_pane_calls.push_back(index); };
        deps.router = &router;
        dispatcher = std::make_unique<InputDispatcher>(std::move(deps));
        dispatcher->connect(window);
    }
};

} // namespace

TEST_CASE("chord: prefix then matching second key fires action and is NOT forwarded",
    "[input_dispatcher][chord]")
{
    ChordE2ESetup setup;

    // Press Ctrl+B (prefix). This is swallowed by the dispatcher.
    setup.window.on_key(KeyEvent{ 0, SDLK_B, kModCtrl, true });
    REQUIRE(setup.host.key_events.empty());
    REQUIRE(setup.split_vertical_calls == 0);

    // Press Shift+\ (the chord's second key). Action fires; key not forwarded.
    setup.window.on_key(KeyEvent{ 0, SDLK_BACKSLASH, kModShift, true });
    REQUIRE(setup.split_vertical_calls == 1);
    REQUIRE(setup.host.key_events.empty());
}

TEST_CASE("chord: prefix then unrecognised key cancels chord and forwards the second key",
    "[input_dispatcher][chord]")
{
    ChordE2ESetup setup;

    setup.window.on_key(KeyEvent{ 0, SDLK_B, kModCtrl, true });
    setup.window.on_key(KeyEvent{ 0, SDLK_A, kModNone, true });

    REQUIRE(setup.split_vertical_calls == 0);
    REQUIRE(setup.host.key_events.size() == 1);
    REQUIRE(setup.host.key_events[0].keycode == SDLK_A);
}

TEST_CASE("chord: prefix then same prefix again does NOT re-arm; second prefix is forwarded",
    "[input_dispatcher][chord]")
{
    ChordE2ESetup setup;

    // First Ctrl+B activates prefix mode.
    setup.window.on_key(KeyEvent{ 0, SDLK_B, kModCtrl, true });
    // Second Ctrl+B: in the chord state, the dispatcher tries to match a chord
    // binding whose second key is Ctrl+B. None exist, so the chord is dropped
    // and the key falls through to the host.
    setup.window.on_key(KeyEvent{ 0, SDLK_B, kModCtrl, true });
    REQUIRE(setup.split_vertical_calls == 0);
    REQUIRE(setup.host.key_events.size() == 1);
    REQUIRE(setup.host.key_events[0].keycode == SDLK_B);

    // And we are NOT still armed: a subsequent Shift+\ must NOT fire the chord.
    setup.window.on_key(KeyEvent{ 0, SDLK_BACKSLASH, kModShift, true });
    REQUIRE(setup.split_vertical_calls == 0);
}

TEST_CASE("chord: modifier-only keys during prefix do not cancel chord state",
    "[input_dispatcher][chord]")
{
    ChordE2ESetup setup;

    setup.window.on_key(KeyEvent{ 0, SDLK_B, kModCtrl, true });
    // Pretend the user is now holding Shift to type | — SDL fires a key-down
    // for the modifier itself first. This must NOT cancel prefix mode, otherwise
    // chords whose second key requires a modifier would be impossible.
    setup.window.on_key(KeyEvent{ 0, SDLK_LSHIFT, kModShift, true });
    setup.window.on_key(KeyEvent{ 0, SDLK_BACKSLASH, kModShift, true });

    REQUIRE(setup.split_vertical_calls == 1);
    REQUIRE(setup.host.key_events.empty());
}

TEST_CASE("chord: Return binding also fires on keypad Enter",
    "[input_dispatcher][chord]")
{
    E2ESetup setup({
        { "toggle_copy_mode", SDLK_S, kModCtrl, SDLK_RETURN, kModNone },
    });

    setup.window.on_key(KeyEvent{ 0, SDLK_S, kModCtrl, true });
    setup.window.on_key(KeyEvent{ 0, SDLK_KP_ENTER, kModNone, true });

    REQUIRE(setup.host.dispatched_actions == std::vector<std::string>{ "toggle_copy_mode" });
    REQUIRE(setup.host.key_events.empty());
}

TEST_CASE("chord: state is cleared after a successful chord — next chord-key alone is inert",
    "[input_dispatcher][chord]")
{
    ChordE2ESetup setup;

    // Fire the chord.
    setup.window.on_key(KeyEvent{ 0, SDLK_B, kModCtrl, true });
    setup.window.on_key(KeyEvent{ 0, SDLK_BACKSLASH, kModShift, true });
    REQUIRE(setup.split_vertical_calls == 1);

    // Now press Shift+\ on its own. It must reach the host and must NOT fire
    // the chord again — the chord lookup in gui_action_for_key_event explicitly
    // skips bindings with prefix_key != 0.
    setup.window.on_key(KeyEvent{ 0, SDLK_BACKSLASH, kModShift, true });
    REQUIRE(setup.split_vertical_calls == 1);
    REQUIRE(setup.host.key_events.size() == 1);
    REQUIRE(setup.host.key_events[0].keycode == SDLK_BACKSLASH);
}

TEST_CASE("chord: switching focused host mid-chord does NOT clear prefix state",
    "[input_dispatcher][chord]")
{
    // Documents current behaviour: set_host() (called on pane focus changes)
    // intentionally does not reset the chord state machine. The chord then
    // resolves against the *new* host. If this becomes a usability problem,
    // file a follow-up bug — this test is the regression guard either way.
    ChordE2ESetup setup;

    setup.window.on_key(KeyEvent{ 0, SDLK_B, kModCtrl, true });
    setup.dispatcher->set_host(&setup.other_host);
    setup.window.on_key(KeyEvent{ 0, SDLK_BACKSLASH, kModShift, true });

    REQUIRE(setup.split_vertical_calls == 1);
    REQUIRE(setup.other_host.key_events.empty());
    REQUIRE(setup.host.key_events.empty());
}

TEST_CASE("chord: prefix then zero enters pane-select submode and updates indicator",
    "[input_dispatcher][chord]")
{
    ChordE2ESetup setup;

    setup.window.on_key(KeyEvent{ 0, SDLK_B, kModCtrl, true });
    setup.window.on_key(KeyEvent{ 0, SDLK_0, kModNone, true });

    REQUIRE(setup.activate_pane_calls.empty());
    REQUIRE(setup.host.key_events.empty());

    const auto state = setup.dispatcher->chord_indicator_state(std::chrono::steady_clock::now());
    REQUIRE(state.visible());
    REQUIRE(state.text == "Ctrl+B 0");
}

TEST_CASE("chord: prefix then zero then digit activates pane and starts fade-out",
    "[input_dispatcher][chord]")
{
    ChordE2ESetup setup;

    setup.window.on_key(KeyEvent{ 0, SDLK_B, kModCtrl, true });
    setup.window.on_key(KeyEvent{ 0, SDLK_0, kModNone, true });
    setup.window.on_key(KeyEvent{ 0, SDLK_3, kModNone, true });

    REQUIRE(setup.activate_pane_calls == std::vector<int>{ 3 });
    REQUIRE(setup.host.key_events.empty());

    const auto state = setup.dispatcher->chord_indicator_state(std::chrono::steady_clock::now());
    REQUIRE(state.visible());
    REQUIRE(state.text == "Ctrl+B 0 3");
}

TEST_CASE("chord: pending prefix times out and indicator eventually clears",
    "[input_dispatcher][chord]")
{
    ChordE2ESetup setup;

    setup.window.on_key(KeyEvent{ 0, SDLK_B, kModCtrl, true });
    const auto now = std::chrono::steady_clock::now();

    setup.dispatcher->set_chord_indicator_fade_ms(700);

    REQUIRE(setup.dispatcher->update(now + std::chrono::milliseconds(1600), 1500));
    REQUIRE(setup.dispatcher->chord_indicator_state(now + std::chrono::milliseconds(1600)).visible());

    REQUIRE(setup.dispatcher->update(now + std::chrono::milliseconds(2400), 1500));
    REQUIRE_FALSE(setup.dispatcher->chord_indicator_state(now + std::chrono::milliseconds(2400)).visible());
}
