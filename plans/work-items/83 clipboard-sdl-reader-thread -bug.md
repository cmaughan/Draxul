# WI 83 — clipboard-sdl-reader-thread

**Type:** bug  
**Priority:** 0 (data race / crash)  
**Source:** review-bugs-consensus.md §C2 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`NvimRpc::dispatch_rpc_request()` (`libs/draxul-nvim/src/rpc.cpp:248`) runs on the reader thread. Its `on_request` callback (set in `libs/draxul-host/src/nvim_host.cpp:43`) calls `NvimHost::handle_rpc_request()` which at line 394 calls `window().clipboard_text()` → `SDL_GetClipboardText()`. SDL3 clipboard access is main-thread-only. This races with any main-thread SDL event processing and is a formal data race (UB).

The race fires on every neovim paste operation (`clipboard_get` RPC request).

---

## Investigation

- [ ] Read `libs/draxul-nvim/src/rpc.cpp:230–255` — confirm `dispatch_rpc_request` executes on the reader thread.
- [ ] Read `libs/draxul-host/src/nvim_host.cpp:40–45` and `392–403` — confirm `handle_rpc_request` calls SDL clipboard APIs.
- [ ] Survey the rest of `handle_rpc_request` and `handle_clipboard_set` for any other SDL or non-thread-safe calls made from the reader thread.
- [ ] Decide on fix approach: (a) promise/future to marshal clipboard read to main thread, (b) main-thread cache behind a mutex.

---

## Fix Strategy

- [ ] Cache the clipboard text on the main thread (refreshed each time `drain_notifications()` runs or on each SDL clipboard event) behind a `std::mutex`.
- [ ] In `handle_rpc_request()`, lock the cache mutex and return the cached text — no SDL call from the reader thread.
- [ ] Alternatively, enqueue a clipboard-fetch token in the notification queue and resolve it before replying. (More complex; prefer the cache approach.)
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] No SDL clipboard API is called from the reader thread.
- [ ] Paste still delivers correct text under ThreadSanitizer (or equivalent analysis).
- [ ] Smoke test passes.

---

## Interdependencies

- **WI 89** (rpc-notification-callback-under-lock) — both touch `rpc.cpp`; combine into one pass to reduce conflicts.
- Related icebox item `plans/work-items-icebox/13 nvim-clipboard-failure -test.md` — add a regression test once fixed.
