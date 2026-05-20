# 63 Host Abstraction And PowerShell Host

## Why This Exists

`App` currently owns both the generic window/renderer/font loop and the Neovim-specific host lifecycle. That makes it impossible to choose between multiple content hosts at startup without growing `app/app.cpp` into an even larger orchestration hotspot.

The next feature step is to treat Neovim as one host implementation among several, then add a Windows PowerShell host that renders through the same grid/font/renderer pipeline.

## Goal

Launch Draxul with either:

- an `nvim` host, preserving current behavior
- a Windows PowerShell host, backed by a terminal emulator path

The architecture should support future split/tile layouts, but this item only needs a single active host filling the content region.

## Implementation Plan

- [x] Create `plans/work-items/` and record this active item.
- [x] Read `app/`, `draxul-renderer`, `draxul-grid`, `draxul-nvim`, and layout code to identify the host seam.
- [x] Add a new host abstraction and factory with a startup-selectable host kind.
- [x] Move the existing Neovim lifecycle behind an `NvimHost` implementation with no behavior change.
- [x] Keep `App` responsible for window ownership, renderer ownership, text service, debug panel, and the top-level run loop.
- [x] Introduce a shared grid-host core for grid state, highlights, grid rendering pipeline, cursor state, and layout-driven resize bookkeeping.
- [x] Add Windows PowerShell hosting using ConPTY and a terminal emulator path that feeds the shared grid host core.
- [x] Add CLI startup selection for `--host nvim|powershell`.
- [x] Generalize test/render-test startup options so host choice is explicit instead of Neovim-only.
- [x] Add or update tests for host selection, host factory behavior, terminal parsing/input basics, and Windows smoke coverage.
- [x] Run relevant tests/builds and a final `clang-format` pass on touched source files.

## Validation

- `cmake --build build --config Release --parallel`
- `ctest --test-dir build --build-config Release --output-on-failure`
- waited Windows smoke run: `draxul.exe --console --smoke-test --host powershell`

## Sub-Agent Split

Single agent. The work cuts across `app/`, new host libraries, startup options, and Windows-only terminal plumbing.
