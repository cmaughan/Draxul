# WI 21 — Config reload under host activity

**Type:** test  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 5

---

## Goal

Verify that `reload_config()` triggered while synthetic key events are being processed does not cause lost events, race conditions, or inconsistent config state.

---

## What to test

- [ ] Construct an `AppConfig`-backed config document and a fake `InputDispatcher`.
- [ ] Start a background thread that fires synthetic key events at high frequency.
- [ ] Call `reload_config()` from the main thread concurrently.
- [ ] Assert all key events are processed (none dropped or duplicated).
- [ ] Assert config state after reload is the new config, not a mix of old+new.
- [ ] Assert no crash or assertion failure under TSan.

**Config-specific sub-cases:**
- [ ] Partial reload: new config has invalid font section but valid keybindings — assert keybindings update, font does not change, and a warning toast is shown.
- [ ] Reload of unchanged file — assert no visible flash or grid redraw.

---

## Implementation notes

- No GPU renderer needed — fake renderer + config layer only.
- The test should run on both macOS and Windows since config reload is platform-shared.
- Run under TSan (`cmake --preset mac-tsan`) for the concurrent access case.
- Place in `tests/config_reload_test.cpp`.

---

## Interdependencies

- WI 56 icebox (live-config-reload feature) is the user-facing version of this; this test provides a correctness baseline for that work.
- WI 29 (config-migration-framework feature) may add new reload paths that need this test to be updated.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
