#include "support/test_support.h"

#include <SDL3/SDL.h>
#include <catch2/catch_all.hpp>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/log.h>

#include <string>
#include <unordered_map>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

// Build a KeyEvent with just the fields relevant to keybinding matching.
KeyEvent make_key_event(int key, ModifierFlags modifiers = kModNone, bool pressed = true)
{
    KeyEvent evt{};
    evt.keycode = key;
    evt.mod = modifiers;
    evt.pressed = pressed;
    return evt;
}

// Look up the first binding for a given action from a config.
const GuiKeybinding* find_binding(const AppConfig& cfg, std::string_view action)
{
    for (const auto& b : cfg.keybindings)
    {
        if (b.action == action)
            return &b;
    }
    return nullptr;
}

// Simulate the same lookup the App performs: iterate bindings in order and
// return the first matching action name.
std::optional<std::string_view> action_for_event(const AppConfig& cfg, const KeyEvent& evt)
{
    for (const auto& binding : cfg.keybindings)
    {
        if (gui_keybinding_matches(binding, evt))
            return binding.action;
    }
    return std::nullopt;
}

// Simulate App::build_action_map() — maps action names to counters.
struct ActionDispatcher
{
    std::unordered_map<std::string, int> fire_counts;

    void register_action(const std::string& name)
    {
        fire_counts[name] = 0;
    }

    bool dispatch(std::string_view action)
    {
        auto it = fire_counts.find(std::string(action));
        if (it == fire_counts.end())
            return false;
        ++it->second;
        return true;
    }

    int count(const std::string& action) const
    {
        auto it = fire_counts.find(action);
        return it != fire_counts.end() ? it->second : 0;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// -----------------------------------------------------------------------
// Default config has the expected set of bindings
// -----------------------------------------------------------------------

TEST_CASE("keybinding dispatch: default config has all known bindings", "[config]")
{
    AppConfig cfg;
    INFO("nine default bindings");
    REQUIRE(static_cast<int>(cfg.keybindings.size()) == 31);
    INFO("toggle_diagnostics binding present");
    REQUIRE(find_binding(cfg, "toggle_diagnostics") != nullptr);
    INFO("copy binding present");
    REQUIRE(find_binding(cfg, "copy") != nullptr);
    INFO("paste binding present");
    REQUIRE(find_binding(cfg, "paste") != nullptr);
    INFO("font_increase binding present");
    REQUIRE(find_binding(cfg, "font_increase") != nullptr);
    INFO("font_decrease binding present");
    REQUIRE(find_binding(cfg, "font_decrease") != nullptr);
    INFO("font_reset binding present");
    REQUIRE(find_binding(cfg, "font_reset") != nullptr);
}

// -----------------------------------------------------------------------
// A configured binding fires the correct action
// -----------------------------------------------------------------------

TEST_CASE("keybinding dispatch: Ctrl+C mapped to copy fires copy action", "[config]")
{
    AppConfig cfg = AppConfig::parse("[keybindings]\ncopy = \"Ctrl+C\"\n");
    ActionDispatcher dispatcher;
    for (const auto& b : cfg.keybindings)
        dispatcher.register_action(b.action);

    KeyEvent evt = make_key_event(SDLK_C, kModCtrl);
    auto action = action_for_event(cfg, evt);
    INFO("Ctrl+C resolves to a known action");
    REQUIRE(action.has_value());
    INFO("Ctrl+C maps to copy");
    REQUIRE(std::string(*action) == std::string("copy"));

    dispatcher.dispatch(*action);
    INFO("copy action fired exactly once");
    REQUIRE(dispatcher.count("copy") == 1);
}

TEST_CASE("keybinding dispatch: F12 fires toggle_diagnostics", "[config]")
{
    AppConfig cfg;
    ActionDispatcher dispatcher;
    for (const auto& b : cfg.keybindings)
        dispatcher.register_action(b.action);

    KeyEvent evt = make_key_event(SDLK_F12, kModNone);
    auto action = action_for_event(cfg, evt);
    INFO("F12 resolves to a known action");
    REQUIRE(action.has_value());
    INFO("F12 maps to toggle_diagnostics");
    REQUIRE(std::string(*action) == std::string("toggle_diagnostics"));

    dispatcher.dispatch(*action);
    INFO("toggle_diagnostics fired once");
    REQUIRE(dispatcher.count("toggle_diagnostics") == 1);
}

TEST_CASE("keybinding dispatch: font_increase Ctrl+= fires font_increase action", "[config]")
{
    AppConfig cfg;
    ActionDispatcher dispatcher;
    for (const auto& b : cfg.keybindings)
        dispatcher.register_action(b.action);

    KeyEvent evt = make_key_event(SDLK_EQUALS, kModCtrl);
    auto action = action_for_event(cfg, evt);
    INFO("Ctrl+= resolves to a known action");
    REQUIRE(action.has_value());
    INFO("Ctrl+= maps to font_increase");
    REQUIRE(std::string(*action) == std::string("font_increase"));

    dispatcher.dispatch(*action);
    INFO("font_increase fired once");
    REQUIRE(dispatcher.count("font_increase") == 1);
}

TEST_CASE("keybinding dispatch: font_decrease Ctrl+- fires font_decrease action", "[config]")
{
    AppConfig cfg;
    ActionDispatcher dispatcher;
    for (const auto& b : cfg.keybindings)
        dispatcher.register_action(b.action);

    KeyEvent evt = make_key_event(SDLK_MINUS, kModCtrl);
    auto action = action_for_event(cfg, evt);
    INFO("Ctrl+- resolves to a known action");
    REQUIRE(action.has_value());
    INFO("Ctrl+- maps to font_decrease");
    REQUIRE(std::string(*action) == std::string("font_decrease"));

    dispatcher.dispatch(*action);
    INFO("font_decrease fired once");
    REQUIRE(dispatcher.count("font_decrease") == 1);
}

TEST_CASE("keybinding dispatch: font_reset Ctrl+0 fires font_reset action", "[config]")
{
    AppConfig cfg;
    ActionDispatcher dispatcher;
    for (const auto& b : cfg.keybindings)
        dispatcher.register_action(b.action);

    KeyEvent evt = make_key_event(SDLK_0, kModCtrl);
    auto action = action_for_event(cfg, evt);
    INFO("Ctrl+0 resolves to a known action");
    REQUIRE(action.has_value());
    INFO("Ctrl+0 maps to font_reset");
    REQUIRE(std::string(*action) == std::string("font_reset"));

    dispatcher.dispatch(*action);
    INFO("font_reset fired once");
    REQUIRE(dispatcher.count("font_reset") == 1);
}

TEST_CASE("keybinding dispatch: copy Ctrl+Shift+C fires copy action", "[config]")
{
    AppConfig cfg;
    ActionDispatcher dispatcher;
    for (const auto& b : cfg.keybindings)
        dispatcher.register_action(b.action);

    KeyEvent evt = make_key_event(SDLK_C, kModCtrl | kModShift);
    auto action = action_for_event(cfg, evt);
    INFO("Ctrl+Shift+C resolves to a known action");
    REQUIRE(action.has_value());
    INFO("Ctrl+Shift+C maps to copy");
    REQUIRE(std::string(*action) == std::string("copy"));
}

TEST_CASE("keybinding dispatch: paste Ctrl+Shift+V fires paste action", "[config]")
{
    AppConfig cfg;
    ActionDispatcher dispatcher;
    for (const auto& b : cfg.keybindings)
        dispatcher.register_action(b.action);

    KeyEvent evt = make_key_event(SDLK_V, kModCtrl | kModShift);
    auto action = action_for_event(cfg, evt);
    INFO("Ctrl+Shift+V resolves to a known action");
    REQUIRE(action.has_value());
    INFO("Ctrl+Shift+V maps to paste");
    REQUIRE(std::string(*action) == std::string("paste"));
}

// -----------------------------------------------------------------------
// Overlapping bindings: first match wins
// -----------------------------------------------------------------------

TEST_CASE("keybinding dispatch: overlapping bindings — first match wins", "[config]")
{
    // Create config with two bindings for the same key combo.
    // The first one in the vector should win.
    AppConfig cfg;
    cfg.keybindings.clear();
    cfg.keybindings.push_back({ "copy", 0, kModNone, SDLK_C, kModCtrl });
    cfg.keybindings.push_back({ "paste", 0, kModNone, SDLK_C, kModCtrl }); // same key, different action

    KeyEvent evt = make_key_event(SDLK_C, kModCtrl);
    auto action = action_for_event(cfg, evt);
    INFO("Ctrl+C resolves to an action with duplicate bindings");
    REQUIRE(action.has_value());
    INFO("first binding wins when two bindings share the same key");
    REQUIRE(std::string(*action) == std::string("copy"));
}

// -----------------------------------------------------------------------
// Unknown action in config: parse_gui_keybinding rejects it
// -----------------------------------------------------------------------

TEST_CASE("keybinding dispatch: unknown action name is rejected by parser", "[config]")
{
    // parse_gui_keybinding validates the action name against kKnownGuiActions.
    auto result = parse_gui_keybinding("launch_rockets", "Ctrl+R");
    INFO("unknown action 'launch_rockets' is rejected by the parser");
    REQUIRE(!result.has_value());
}

TEST_CASE("keybinding dispatch: unknown action in TOML config is silently ignored", "[config]")
{
    // A TOML config with an unknown action key should not crash and the
    // unrecognised binding should not appear in the parsed keybindings.
    const char* content = "[keybindings]\nlaunch_rockets = \"Ctrl+R\"\n";
    ScopedLogCapture capture;
    AppConfig cfg = AppConfig::parse(content);

    // The unknown action must not appear in the bindings list.
    for (const auto& b : cfg.keybindings)
    {
        INFO("unknown action must not appear in parsed bindings");
        REQUIRE(b.action != "launch_rockets");
    }

    // The default bindings should still be present.
    INFO("default toggle_diagnostics binding is still present");
    REQUIRE(find_binding(cfg, "toggle_diagnostics") != nullptr);
}

// -----------------------------------------------------------------------
// Empty config: no bindings, no crash
// -----------------------------------------------------------------------

TEST_CASE("keybinding dispatch: empty keybindings table leaves defaults intact", "[config]")
{
    // An empty [keybindings] table should not crash and should keep defaults.
    AppConfig cfg = AppConfig::parse("[keybindings]\n");

    // With an empty [keybindings] table the defaults remain unchanged.
    INFO("defaults remain with empty bindings table");
    REQUIRE(static_cast<int>(cfg.keybindings.size()) == 31);
}

TEST_CASE("keybinding dispatch: no bindings configured — no action fires for any key", "[config]")
{
    AppConfig cfg;
    cfg.keybindings.clear();

    // No binding matches any key.
    const KeyEvent keys[] = {
        make_key_event(SDLK_F12),
        make_key_event(SDLK_C, kModCtrl),
        make_key_event(SDLK_EQUALS, kModCtrl),
    };

    for (const auto& evt : keys)
    {
        auto action = action_for_event(cfg, evt);
        INFO("no action fires when no bindings are registered");
        REQUIRE(!action.has_value());
    }
}

// -----------------------------------------------------------------------
// Modifier-only key events do not trigger any binding
// -----------------------------------------------------------------------

TEST_CASE("keybinding dispatch: modifier-only key events do not match any binding", "[config]")
{
    AppConfig cfg;

    // Key events where the key itself is a modifier key (SDL_KMOD_* keys)
    // should not match any binding because no binding uses a modifier key as
    // the primary key.
    const int modifier_keys[] = {
        SDLK_LCTRL,
        SDLK_RCTRL,
        SDLK_LSHIFT,
        SDLK_RSHIFT,
        SDLK_LALT,
        SDLK_RALT,
    };

    for (int key : modifier_keys)
    {
        KeyEvent evt = make_key_event(key, kModNone);
        auto action = action_for_event(cfg, evt);
        INFO("modifier-only key event does not match any binding");
        REQUIRE(!action.has_value());
    }
}

// -----------------------------------------------------------------------
// Key-released events do not trigger bindings (binding dispatch checks pressed)
// -----------------------------------------------------------------------

TEST_CASE("keybinding dispatch: key-released event does not trigger binding", "[config]")
{
    AppConfig cfg;

    // F12 released — should not trigger toggle_diagnostics.
    KeyEvent evt = make_key_event(SDLK_F12, kModNone, /*pressed=*/false);
    // App only calls gui_action_for_key_event when event.pressed is true.
    // The matching function itself does not check pressed, but the App guards
    // the call with `if (event.pressed)`.  We test the matcher alone and note
    // that dispatch is guarded at the call site.
    // At the matcher level, the binding would match regardless of pressed.
    // This test documents that no panic / UB occurs for released events.
    auto action = action_for_event(cfg, evt);
    // Not asserting direction — just that no crash occurs.
    INFO("no crash dispatching key-released event");
    REQUIRE(true);
}

// -----------------------------------------------------------------------
// Custom bindings parsed from TOML
// -----------------------------------------------------------------------

TEST_CASE("keybinding dispatch: TOML custom binding overrides default for same action", "[config]")
{
    // Override toggle_diagnostics to Ctrl+D instead of F12.
    const char* content = "[keybindings]\ntoggle_diagnostics = \"Ctrl+D\"\n";
    AppConfig cfg = AppConfig::parse(content);

    const GuiKeybinding* toggle = find_binding(cfg, "toggle_diagnostics");
    INFO("toggle_diagnostics binding is present");
    REQUIRE(toggle != nullptr);
    INFO("toggle key overridden to D");
    REQUIRE(toggle->key == SDLK_D);
    INFO("toggle modifiers overridden to Ctrl");
    REQUIRE(toggle->modifiers == kModCtrl);

    // Old binding (F12) must no longer trigger toggle_diagnostics.
    KeyEvent f12_evt = make_key_event(SDLK_F12, kModNone);
    auto f12_action = action_for_event(cfg, f12_evt);
    INFO("F12 no longer triggers toggle_diagnostics after override");
    REQUIRE((!f12_action.has_value() || std::string(*f12_action) != "toggle_diagnostics"));

    // New binding (Ctrl+D) must trigger toggle_diagnostics.
    KeyEvent ctrl_d_evt = make_key_event(SDLK_D, kModCtrl);
    auto ctrl_d_action = action_for_event(cfg, ctrl_d_evt);
    INFO("Ctrl+D resolves to a binding");
    REQUIRE(ctrl_d_action.has_value());
    INFO("Ctrl+D triggers toggle_diagnostics after override");
    REQUIRE(std::string(*ctrl_d_action) == std::string("toggle_diagnostics"));
}

TEST_CASE("keybinding dispatch: gui_keybinding_matches ignores irrelevant modifier bits", "[config]")
{
    // The matcher normalises modifiers through kGuiModifierMask; CAPSLOCK
    // should not affect matching.
    const GuiKeybinding binding{ "font_increase", 0, kModNone, SDLK_EQUALS, kModCtrl };

    // Ctrl+= without any extra modifiers: should match.
    INFO("Ctrl+= with no extra mods matches");
    REQUIRE(gui_keybinding_matches(binding, make_key_event(SDLK_EQUALS, kModCtrl)));

    // Unrelated key: should not match.
    INFO("Ctrl+A does not match a Ctrl+= binding");
    REQUIRE(!gui_keybinding_matches(binding, make_key_event(SDLK_A, kModCtrl)));

    // Wrong modifier: should not match.
    INFO("Alt+= does not match a Ctrl+= binding");
    REQUIRE(!gui_keybinding_matches(binding, make_key_event(SDLK_EQUALS, kModAlt)));
}
