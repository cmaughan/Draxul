# 66 Terminal Selection Clipboard And Mouse Modes

## Why This Exists

The current terminal host has paste support, but it does not support text selection or copy. It also does not report mouse activity to terminal applications.

Those two gaps are tightly related. A usable terminal needs native selection/copy for shell output, but terminal applications also need xterm mouse reporting modes. The implementation needs a clear policy for when the GUI owns the mouse and when the terminal app does.

## Goal

Add terminal text selection/copy plus xterm-compatible mouse reporting, with predictable ownership rules between GUI selection behavior and app-requested mouse modes.

## Implementation Plan

- [x] Read the current PowerShell host input path and identify where mouse events are currently dropped.
- [x] Add terminal mouse mode state (`1000`, `1002`, `1003`, `1006`, and related focus/mouse-report toggles as needed).
- [x] Emit the correct escape sequences for button presses, motion, wheel input, and focus changes when the terminal app enables mouse reporting.
- [x] Add native selection state for click-drag selection when the terminal app is not claiming mouse input.
- [x] Add copy extraction from selected terminal text, including multi-line selections and Unicode clusters.
- [x] Render selection highlighting through the existing grid/overlay pipeline instead of inventing a parallel renderer path.
- [x] Define and document the routing policy between local selection behavior and app-owned mouse mode behavior.
- [x] Add tests for reported mouse sequences and copied text extraction.
- [x] Run `ctest`, manual Windows validation, and a final `clang-format` pass.

## Sub-Agent Split

Two-agent split is reasonable:

- terminal mouse-reporting protocol work
- native selection/copy extraction and rendering
