# 07 Platform Text And Display Integration

## Why This Exists

The core platform integration is decent now, but the next real platform-facing gap is quality rather than raw functionality.

Reviewed issues and follow-ups:

- richer clipboard integration is still incomplete
- IME plumbing exists, but composition display is still not a real user-facing feature
- macOS multi-monitor DPI handling still needs verification/fixup around active-display choice
- title/clear-color/display polish is still uneven

## Goal

Improve real platform integration without reopening the core architecture.

## Implementation Plan

1. Clipboard provider integration.
   - move from shortcuts-only behavior toward proper Neovim clipboard provider semantics where practical
2. IME composition visibility.
   - surface composition/preedit state instead of only wiring low-level events
3. Display-following DPI behavior.
   - especially on macOS, ensure font metrics follow the display actually hosting the window
4. Small presentation cleanups.
   - title behavior
   - default background/clear-color alignment with Neovim defaults

## Tests

- clipboard/provider behavior where feasible
- DPI-provider test on the window abstraction path
- render snapshot additions if clear-color/title-visible UI changes land

## Suggested Slice Order

1. DPI correctness
2. clipboard/provider upgrade
3. IME composition UI
4. presentation cleanup

## Sub-Agent Split

- platform/window agent for DPI and IME plumbing
- Neovim/input agent for clipboard-provider behavior
