# 00 startup-config-save-on-failure -bug

**Priority:** HIGH
**Type:** Bug
**Raised by:** Gemini, GPT, Claude (unanimous)
**Model:** claude-sonnet-4-6

---

## Problem

`App::initialize()` arms a rollback scope that calls `shutdown()` on any initialisation failure. `shutdown()` unconditionally persists config when `save_user_config` is true. This means a failed window creation, renderer init, font load, or host init can overwrite the user's `config.toml` with partial or default state — silently replacing a working config with a broken one.

Key locations:
- `app/app.cpp:73` — rollback scope armed
- `app/app.cpp:542` — `shutdown()` writes config

---

## Fix Plan

- [x] Read `app/app.cpp` fully, focusing on `initialize()`, the rollback scope, and `shutdown()`.
- [x] Determine where `save_user_config` is set and whether it can be false during a failed init.
- [x] Add a guard: config must not be saved if `initialize()` did not complete successfully. Options:
  - Set a `bool init_completed_` flag; only save in `shutdown()` if true.
  - Or: don't call `save_config()` from inside the rollback path — only call it from a clean-exit path.
- [x] Ensure the rollback still tears down GPU/window/process resources correctly (shutdown correctness is orthogonal to config persistence).
- [x] Build and run smoke test: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [x] Run ctest.

---

## Acceptance

- A deliberate init failure (e.g., bad renderer config) does not modify `config.toml`.
- Normal exit still saves config as before.

---

## Interdependencies

- Validates via **06-test** (startup-config-not-saved) and **14-test** (config-partial-init-save).
- No upstream dependencies.
