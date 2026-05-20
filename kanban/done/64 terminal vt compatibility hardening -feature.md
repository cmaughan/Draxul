# 64 Terminal VT Compatibility Hardening

## Why This Exists

The current PowerShell host is intentionally minimal. It is enough to launch a shell, render prompt/output, handle colors, and process basic cursor movement and erase commands, but it is not yet a robust terminal emulator.

That gap will show up immediately once the host is asked to run richer console programs, future bash support, or full-screen terminal UIs. The current implementation in `libs/draxul-host/src/powershell_host_win.cpp` needs broader VT/xterm coverage and a less ad-hoc screen-state model.

## Goal

Support the core VT/xterm semantics needed for interactive shell applications and future non-PowerShell terminal hosts, without regressing the existing grid-based renderer path.

## Implementation Plan

- [x] Read `libs/draxul-host/src/powershell_host_win.cpp` and inventory the currently supported CSI / OSC / DEC sequences.
- [x] Extract the terminal screen state from the host into a reusable terminal model object instead of keeping it embedded in the PowerShell host. (Done as `TerminalHostBase` in `terminal_host_base.h/cpp`; `PowerShellHost` and `ShellHost` both inherit from it.)
- [x] Add support for alternate-screen semantics, including distinct main-screen vs alternate-screen buffers.
- [x] Add support for line/character insertion and deletion commands (`IL`, `DL`, `ICH`, `DCH`, `ECH`) and any missing cursor-positioning variants used by common TUIs.
- [x] Add scroll-region handling and vertical scrolling commands beyond the current minimal viewport scroll behavior.
- [x] Add wrap/origin/mode state handling that is explicit rather than implied by the current parser shortcuts.
- [x] Keep title updates, SGR colors, and current basic prompt rendering behavior working as-is while broadening the parser.
- [x] Add byte-stream driven tests for the new VT behaviors, including alternate screen entry/exit and insert/delete semantics.
- [x] Run `ctest`, PowerShell smoke validation, and a final `clang-format` pass.

## Sub-Agent Split

Single agent, or two agents with a clean split:

- parser/state-machine work
- terminal screen-model and test coverage work
