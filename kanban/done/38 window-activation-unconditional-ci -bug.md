# 38 Window Activation Unconditional in CI

## Why This Exists

`App` sets `pending_window_activation_ = options_.activate_window_on_startup` which defaults to `true`. In headless / CI render-test runs, this causes the window to steal focus from whatever the operator or CI runner has in the foreground. This is annoying in local runs and can break CI setups that use screen recorders or VNC.

Identified by: **Claude** (worst features #10). Related: **GPT** flags `pending_window_activation_` as a workaround that should be event-driven.

## Goal

Default `activate_window_on_startup` to `false` when running in a render-test or smoke-test context. Normal interactive launches should still activate.

## Implementation Plan

- [x] Read `app/app.h`, `app/app.cpp`, and `app/main.cpp` to understand `AppOptions` and how render/smoke test paths are invoked.
- [x] In `AppOptions` (or `app/main.cpp`), set `activate_window_on_startup = false` when `--render-test` or `--smoke-test` CLI flags are active.
- [x] Ensure the interactive startup path still activates by default.
- [x] Run `ctest` and `clang-format` on touched files.

## Sub-Agent Split

Single agent. Confined to `main.cpp` / `app.cpp` / `AppOptions`.
