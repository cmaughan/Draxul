# 19 shell-pane-cwd-osc7 -feature

**Priority:** LOW
**Type:** Feature (user-facing, shell pane title)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

Modern shells (zsh, bash with proper config, fish) emit the OSC 7 escape sequence (`ESC ] 7 ; file://hostname/path ST`) on every directory change. Draxul does not currently hook this sequence to update the pane title bar with the current working directory. This is a low-cost, high-visibility improvement — users lose context on the current directory unless they look at the shell prompt.

---

## Implementation Plan

- [x] Search for OSC sequence handling in the VT parser / terminal host code (`libs/draxul-host/src/`). Identify whether there is already an OSC dispatch mechanism.
- [x] If OSC dispatch exists: find where OSC sequences are routed after parsing. Add a case for OSC code `7`.
- [x] If OSC dispatch does not exist: add a minimal OSC parser to the terminal byte stream processing path. Only needs to handle the standard `ESC ] N ; ST` framing.
- [x] Add an `on_osc_7(std::string_view path)` callback to `TerminalHostBase` (or the appropriate base class). Default implementation: no-op.
- [x] In `ShellHost` (or equivalent): override `on_osc_7()` to update the pane title via whatever title-update API the host or window uses.
- [x] Test manually: open a shell pane, `cd /tmp`, verify the pane title updates.
- [x] Build and run smoke test.

---

## Acceptance

- `cd`-ing to a directory in a shell pane updates the pane title bar to show the new path.
- Shells that do not emit OSC 7 are not broken (no title update, same as today).
- OSC sequences other than 7 are unaffected.

---

## Interdependencies

- No upstream blockers; self-contained.

---

*claude-sonnet-4-6*
