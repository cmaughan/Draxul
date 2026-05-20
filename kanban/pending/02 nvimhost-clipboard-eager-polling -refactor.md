# WI 136: NvimHost Clipboard Eager Polling Refactor

**Type:** refactor
**Priority:** 02
**Raised by:** GPT (`review-latest.gpt.md`, finding #4)
**Depends on:** None (targeted change in `nvim_host.cpp`)

---

## Problem

`NvimHost::pump()` calls `window().clipboard_text()` on every frame on the main thread. GPT notes this at `libs/draxul-host/src/nvim_host.cpp:101`.

`window().clipboard_text()` ultimately calls into SDL's clipboard API, which may perform an IPC round-trip to the OS clipboard manager (particularly on macOS with `NSPasteboard` and on Linux with X11). Even if the result is cached in `SdlWindow`, the cache check and potential staleness check happen in the hot render loop on every frame — typically 60–120 times per second.

This is wasteful when the clipboard has not changed. It also makes the hot path harder to reason about because clipboard state can vary between frames for reasons unrelated to user input.

---

## Investigation Steps

- [ ] Read `libs/draxul-host/src/nvim_host.cpp` around line 101 to understand what triggers the poll and what it does with the result (likely updating a cached value for the `+` and `*` Neovim registers).
- [ ] Read `libs/draxul-window/include/draxul/window.h` and `libs/draxul-window/src/sdl_window.cpp` to understand whether `clipboard_text()` already caches at the SDL level.
- [ ] Check whether `SdlWindow` emits a `ClipboardUpdateEvent` (SDL3 has `SDL_EVENT_CLIPBOARD_UPDATE`) that could be used instead of polling.

---

## Implementation Plan

### Option A — Event-Driven (preferred)

1. Subscribe to `SDL_EVENT_CLIPBOARD_UPDATE` in `SdlWindow::poll_events()`.
2. Route it as a `ClipboardUpdateEvent` through `IWindow::EventCallback` to the host.
3. `NvimHost` receives the event and refreshes its clipboard cache **only** on change.
4. Remove the per-frame `clipboard_text()` call from `pump()`.

**Trade-off:** Requires adding a new event type to `libs/draxul-types/include/draxul/events.h` and threading it through the `EventCallback`. Low risk; follows the existing event pattern.

### Option B — Lazy Pull with Change Detection (fallback if SDL event not reliable)

1. Cache the clipboard string inside `NvimHost`.
2. Poll only every N frames (e.g. every 30 frames ≈ 500ms at 60fps) rather than every frame.
3. If the string changes, propagate the update to Neovim registers.

**Trade-off:** Simpler change, but still polling. Use Option A if `SDL_EVENT_CLIPBOARD_UPDATE` is available on all supported platforms (macOS SDL3 supports it).

---

## Changes Required

- [ ] `libs/draxul-types/include/draxul/events.h` — add `ClipboardUpdateEvent` (Option A only).
- [ ] `libs/draxul-window/src/sdl_window.cpp` — handle `SDL_EVENT_CLIPBOARD_UPDATE`, emit `ClipboardUpdateEvent`.
- [ ] `libs/draxul-host/src/nvim_host.cpp` — remove per-frame poll; subscribe to `ClipboardUpdateEvent` or implement lazy-pull cache.
- [ ] `libs/draxul-host/include/draxul/nvim.h` — update `NvimHost` event callback signature if needed.

---

## Files Likely Involved

- `libs/draxul-host/src/nvim_host.cpp` (primary change)
- `libs/draxul-window/src/sdl_window.cpp`
- `libs/draxul-types/include/draxul/events.h`
- `libs/draxul-window/include/draxul/window.h`

---

## Acceptance Criteria

- [ ] `NvimHost::pump()` no longer calls `clipboard_text()` on every frame.
- [ ] Clipboard integration with Neovim `+` and `*` registers still works correctly after the change (manual smoke test: copy in another app, `"+p` in Neovim pastes the correct text).
- [ ] No performance regression in the render loop (verify with `--log-level debug` timing output).
- [ ] Build passes on both macOS and Windows (`cmake --build build --target draxul`).

---

*Authored by `claude-sonnet-4-6`*
