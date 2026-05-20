# 28 System Clipboard Integration

## Why This Exists

Clipboard in Draxul is currently implemented via hardcoded `Ctrl+Shift+C/V` key handlers in
`wire_window_callbacks()` that call `nvim_eval("getreg('\"')")` and `paste_text()` directly.
This bypasses Neovim's clipboard integration layer entirely:

- The `clipboard` option (`unnamed`, `unnamedplus`) is ignored.
- The `g:clipboard` provider variable is ignored.
- Bracketed paste is not sent.
- Primary selection (X11/Wayland) is not supported.

Users who set `clipboard=unnamed` in their `init.vim` expect yank/paste to flow through the system
clipboard automatically, without needing `Ctrl+Shift+C/V`.

**Source:** `app/app.cpp` — clipboard handlers in `wire_window_callbacks()`.
**Raised by:** Claude (primary), GPT, Gemini (all three identify this).

## Goal

Implement Neovim's clipboard provider protocol so that:
1. When `clipboard=unnamed` or `unnamedplus` is set, Neovim's yank/paste automatically uses
   the system clipboard via `g:clipboard` callbacks into Draxul.
2. Paste from external apps uses bracketed paste (`\x1b[200~...\x1b[201~`) when Neovim supports it.
3. The hardcoded `Ctrl+Shift+C/V` fallback is retained but documented as a manual override,
   not as the primary clipboard mechanism.

## Implementation Plan

- [x] Read Neovim's clipboard documentation (`nvim --help` or `:help clipboard`) to understand the `g:clipboard` provider protocol and the `nvim_paste` API.
- [x] In `execute_startup_commands()`, set `g:clipboard` to a dictionary that declares `copy` and `paste` functions backed by Draxul:
  - `copy['+']` / `copy['*']` → calls an RPC callback that writes to `SDL_SetClipboardText`.
  - `paste['+']` / `paste['*']` → calls an RPC callback that reads `SDL_GetClipboardText` and returns it.
- [x] Implement the RPC callbacks that Neovim will call when clipboard ops occur.
- [x] Wire the clipboard `paste` action to send bracketed paste via `nvim_paste` when pasting from the GUI.
- [x] Remove (or repurpose as explicit fallback) the current `nvim_eval("getreg('\"')")` clipboard path.
- [x] Test manually: set `clipboard=unnamed` in `init.vim`, yank a word, paste in another application.
- [x] Run `ctest --test-dir build`.

## Notes

This requires understanding Neovim's `nvim_call_function` and Vimscript callback registration.
The `g:clipboard` provider is the standard mechanism; see `:help g:clipboard` in Neovim.

## Sub-Agent Split

- One agent on the `g:clipboard` provider setup and RPC callback wiring.
- One agent on bracketed paste and the fallback `Ctrl+Shift+C/V` cleanup.
