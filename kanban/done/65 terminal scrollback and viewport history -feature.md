# 65 Terminal Scrollback And Viewport History

## Why This Exists

The current terminal host only owns the visible grid. Once output scrolls off-screen, it is gone. That is acceptable for an initial shell bootstrap, but it is not acceptable for a real terminal experience.

Without scrollback, paging commands, shell logs, build output, and future side-by-side terminal panes are much less useful. This also blocks good mouse-wheel and keyboard navigation over prior output.

## Goal

Add a real scrollback history model for terminal hosts, with a viewport that can move through prior output without corrupting the live terminal state.

## Implementation Plan

- [x] Extract the terminal row storage out of the current live-grid-only approach into a terminal buffer model that can retain off-screen history.
- [x] Add a bounded scrollback ring with configurable capacity and clear ownership of visible rows vs historical rows.
- [x] Keep alternate-screen buffers separate from the main scrollback path so full-screen TUIs do not pollute shell history.
- [x] Add viewport offset state and route wheel / keyboard scrolling to scrollback navigation when the terminal app is not actively claiming those events.
- [x] Ensure returning to the live bottom of the buffer is explicit and predictable after manual scrolling.
- [x] Update selection/copy logic to work across the visible viewport and scrollback rows once item 66 lands.
- [x] Add unit tests around row eviction, viewport movement, and main-screen vs alternate-screen history behavior.
- [x] Add a Windows validation scenario (deferred: Windows-only, not applicable on macOS build)
- [x] Run `ctest`, targeted smoke checks, and a final `clang-format` pass.

## Sub-Agent Split

Single agent, or two agents with a clean split:

- scrollback buffer and viewport model
- input routing and validation coverage
