# 67 PowerShell ReadLine Parity

## Why This Exists

The current PowerShell host starts with:

`Remove-Module PSReadLine -ErrorAction SilentlyContinue`

That workaround avoids input and console behaviors the current terminal host does not fully support, but it also removes the modern PowerShell editing experience. It is a temporary bootstrap, not the intended final state.

## Goal

Run PowerShell with its normal interactive editing stack intact, including PSReadLine, without startup workarounds and without degrading terminal correctness.

## Implementation Plan

- [x] Read the current default launch path in `libs/draxul-host/src/powershell_host_win.cpp` and remove assumptions that PSReadLine must be disabled.
- [x] Identify which missing terminal features still force the workaround today, especially around input encoding, cursor movement, bracketed paste, mouse/focus reporting, and richer redraw semantics.
- [x] Sequence this item after the critical parts of items 64 and 66 so the terminal host has the protocol coverage PSReadLine expects.
- [x] Re-enable the default PowerShell module/profile path and verify line editing, history navigation, prompt redraw, incremental edits, and completion UI.
- [x] Add targeted smoke coverage for interactive PowerShell startup without the workaround.
- [x] Keep a clear fallback/error path if a shell startup failure occurs, but do not silently drop back to the stripped-down experience.
- [x] Run `ctest`, a waited PowerShell-host smoke check, and a final `clang-format` pass.

## Sub-Agent Split

Single agent. This item is primarily an integration/validation step after the lower-level terminal work lands.

## What Was Done

- Removed `-NoProfile` and `Remove-Module PSReadLine -ErrorAction SilentlyContinue` from the default
  PowerShell launch args in `powershell_host_win.cpp`. The new default lets the user profile load
  normally (PSReadLine included) and only sets UTF-8 encoding before dropping to the interactive prompt.
- Added `ESC 7` / `ESC 8` (DECSC / DECRC) support to `VtParser` (`on_esc` callback) and
  `TerminalHostBase` (`handle_esc`). PSReadLine uses these for cursor save/restore during line redraws.
- Added `tests/powershell_host_tests.cpp` with six targeted scenarios covering DECSC/DECRC round-trips,
  bracketed paste enable/disable, and a PSReadLine-style startup sequence end-to-end.
- Existing fallback (`pwsh.exe` → `powershell.exe`) is retained unchanged.
