# Bracketed Paste Mode Not Implemented

**Type:** bug
**Priority:** 05
**Raised by:** Claude

## Summary

There is no handling for DECSET 2004 (`\e[?2004h/l`) in `libs/draxul-host/src/terminal_host_base.cpp`. When bracketed paste mode is active in a shell or editor, multi-line text pasted into the terminal should be wrapped in `\e[200~...\e[201~` escape sequences so the receiver can distinguish pasted text from typed input. Without this, pasting multi-line content causes each newline to be interpreted as pressing Enter, submitting incomplete or multiple commands.

## Background

Bracketed paste mode is universally expected by modern shells (bash with readline, zsh, fish, PSReadLine in PowerShell) and editors (vim, neovim, emacs). When a program enables it with `\e[?2004h`, the terminal should wrap any pasted text with `\e[200~` before and `\e[201~` after. This allows the receiver to handle the paste atomically rather than treating each line as a separate command. The absence of this feature means that pasting a multi-line shell command or code snippet sends multiple Enter presses, causing shells to attempt to execute incomplete command fragments.

## Implementation Plan

### Files to modify
- `libs/draxul-host/src/terminal_host_base.cpp` — add DECSET 2004 handler to set/clear `bracketed_paste_mode_` flag; modify the paste path to wrap content when the flag is set
- `libs/draxul-host/src/terminal_host_base.h` — add `bracketed_paste_mode_` bool field

### Steps
- [x] Add `bracketed_paste_mode_` bool member to `TerminalHostBase` (default false)
- [x] In the DECSET handler, recognise mode 2004: `\e[?2004h` sets the flag, `\e[?2004l` clears it
- [x] Find the paste entry point (the method called when the user pastes, typically invoked from `App` via a keybinding or clipboard event)
- [x] In the paste method, check `bracketed_paste_mode_`: if true, prefix pasted text with `\e[200~` and append `\e[201~` before writing to the terminal input stream
- [x] Ensure the bracketed sequence is sent as a single atomic write to avoid interleaving with other input
- [ ] Test with bash, zsh, and neovim: multi-line paste should arrive as a single chunk without triggering intermediate command execution

## Depends On
- None

## Blocks
- `12 terminal-vt-sgr-completeness -test.md` — SGR completeness tests cover adjacent VT parsing surface area

## Notes
The paste path in Draxul is likely triggered from an SDL clipboard event or a keybinding action. Trace from the `paste` keybinding action in `app/app.cpp` or `app/app_support` to find the write-to-terminal call. The bracketed escape sequences must survive any UTF-8 or special-character filtering in the write path.

> Work item produced by: claude-sonnet-4-6
