# WI 127 — NvimHost Dispatch Handler Consolidation

**Type:** Refactor  
**Severity:** Low–Medium (maintainability; dispatch table is a growth point)  
**Source:** Claude review, Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem

Beyond the `open_file_at_type:` duplication (fixed by **WI 115**), `NvimHost::dispatch_action()` has accumulated multiple action handlers that share similar structure but are written as independent if/else chains. This makes adding a new action a copy-paste exercise with no compile-time enforcement.

From Claude: "The split/horizontal and split/vertical handlers are nearly identical — textbook code duplication. Adding a new action requires 4-step changes across separate files with no compile-time enforcement."

This WI targets the overall dispatch table structure in `NvimHost`, complementing the broader `App::wire_gui_actions()` cleanup already tracked in `15 guiactionhandler-dispatch-map -refactor.md`.

---

## Fix Strategy

1. Introduce a `NvimHost` dispatch table: `static const std::unordered_map<std::string_view, Handler> k_dispatch_table`
2. Each handler is a small free function or lambda in the same file
3. `dispatch_action()` looks up the handler and calls it — no if/else chain
4. Compile-time: add a static_assert or test that lists all registered actions and verifies no duplicates

```cpp
static const std::unordered_map<std::string_view, std::function<void(NvimHost&, const ActionArgs&)>>
    k_nvim_handlers = {
        {"open_file_at_type:", handle_open_file},
        {"focus_window:", handle_focus_window},
        // ...
    };
```

---

## Investigation Steps

- [ ] After **WI 115** lands, audit `NvimHost::dispatch_action()` for all handlers
- [ ] Identify which are nvim-specific vs. generic (generic ones should probably be in a base class or `HostManager`)
- [ ] Check if action name strings are typed/validated elsewhere (link to `15 guiactionhandler-dispatch-map`)

---

## Acceptance Criteria

- [ ] `dispatch_action()` contains no if/else chain — only a dispatch-table lookup
- [ ] Action names appear in exactly one place (the table)
- [ ] No duplicate handlers
- [ ] Existing dispatch tests pass
- [ ] CI green

---

## Interdependencies

- **Depends on WI 115** (dedup first so the table starts clean)
- Related to **WI 15** (`guiactionhandler-dispatch-map -refactor`) — coordinate to avoid conflicting approaches
