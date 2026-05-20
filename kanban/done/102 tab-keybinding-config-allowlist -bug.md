# WI 102 — tab-keybinding-config-allowlist

**Type:** bug
**Priority:** 1 (HIGH — user-defined tab keybindings silently discarded on config save/load)
**Source:** review-consensus.md §2 [GPT]
**Produced by:** claude-sonnet-4-6

---

## Problem

Five tab-management action names (`new_tab`, `close_tab`, `next_tab`, `prev_tab`, `activate_tab:1`…`activate_tab:9`) appear in the *default* keybinding set shipped with Draxul, but they are absent from the config serializer and parser allowlists.

Consequences:
1. A user who customises any of these bindings in `config.toml` will have their custom binding silently **ignored** on load (the parser allowlist does not recognise the name).
2. If the app ever rewrites `config.toml`, the serializer will **drop** these entries from the output, destroying the user's custom bindings.

**Files:**
- `libs/draxul-config/src/app_config_io.cpp:33` — serializer action-name allowlist
- `libs/draxul-config/src/app_config_io.cpp:442` — write path
- `libs/draxul-config/src/app_config_io.cpp:543` — read/merge path
- `libs/draxul-config/src/keybinding_parser.cpp:19` — parser allowlist

---

## Investigation

- [ ] Read `libs/draxul-config/src/app_config_io.cpp:1–60` — find the allowlist array/map; confirm `new_tab`, `close_tab`, `next_tab`, `prev_tab` are absent.
- [ ] Read `libs/draxul-config/src/keybinding_parser.cpp:1–40` — find the parser allowlist; confirm the same names are missing.
- [ ] Grep for `activate_tab` in `app_config_io.cpp` and `keybinding_parser.cpp` to check whether parameterised forms like `activate_tab:1` are handled at all.
- [ ] Check `GuiActionHandler` and `InputDispatcher` to confirm the full canonical list of action names that users can legitimately bind.

---

## Fix Strategy

- [ ] Add `new_tab`, `close_tab`, `next_tab`, `prev_tab` to both the serializer and parser allowlists.
- [ ] Decide how `activate_tab:N` (parameterised) should be handled:
  - Option A: add `activate_tab` as a prefix-matched name and parse the `:N` suffix.
  - Option B: add all nine concrete forms `activate_tab:1` … `activate_tab:9` to the allowlist.
- [ ] Write a round-trip test (WI 106) to lock in the fix.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run: `ctest --test-dir build -R config`
- [ ] Run smoke: `py do.py smoke`

---

## Acceptance Criteria

- [ ] `new_tab`, `close_tab`, `next_tab`, `prev_tab` round-trip through `config.toml` without loss.
- [ ] Custom bindings for these actions are loaded correctly and not dropped on rewrite.
- [ ] WI 106 round-trip test passes.

---

## Interdependencies

- **WI 103** (activate-tab-dead-palette-command) — both concern `activate_tab` handling; address together.
- **WI 106** (tab-keybinding-config-roundtrip -test) — acceptance test for this fix.
- **WI 71** (gui-action-single-source-of-truth -refactor) — the root cause is that action names are registered in multiple places; WI 71's registry consolidation will prevent recurrence.
