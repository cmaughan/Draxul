# 15 guiactionhandler-dispatch-map -refactor

**Priority:** LOW
**Type:** Refactor (maintainability, self-documenting)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`GuiActionHandler::execute()` uses an `if-else` chain over string comparisons (`action == "toggle_diagnostics"`, `action == "copy"`, etc.). At 9 actions this is manageable, but the pattern grows linearly, is easy to mistype (no compile-time check for string literals), and makes it hard to enumerate all supported actions at runtime. Replacing with a static `unordered_map<string_view, function<void()>>` built once at construction is O(1), self-documenting, and makes omitted handlers detectable.

---

## Implementation Plan

- [ ] Read `app/gui_action_handler.h` and `app/gui_action_handler.cpp` to understand the current dispatch and all supported action names.
- [ ] Change `execute()` to use a dispatch table:
  ```cpp
  // In constructor or lazy-init:
  static const std::unordered_map<std::string_view, std::function<void(GuiActionHandler&)>> kActions = {
      {"toggle_diagnostics", [](auto& h) { h.toggle_diagnostics(); }},
      {"copy",               [](auto& h) { h.copy(); }},
      // ...
  };
  ```
  Or use member function pointers if the actions are all simple method calls.
- [ ] Replace the `if-else` chain in `execute()` with a map lookup + call.
- [ ] Log a WARN for unknown action names (retain the current behavior).
- [ ] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [ ] Run `clang-format`.

---

## Acceptance

- `execute()` body is a map lookup, not an `if-else` chain.
- Behavior is identical for all existing action names.
- Unknown action names produce the same warning as before.

---

## Interdependencies

- No upstream blockers; self-contained.

---

*claude-sonnet-4-6*
