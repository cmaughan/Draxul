# 20 searchable-scrollback -feature

**Priority:** LOW
**Type:** Feature (quality-of-life, terminal usability)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`LocalTerminalHost` has scrollback buffer infrastructure (`scrollback_buffer.h`), but there is no way to search within it. Users must scroll manually to find text. A pattern-match search that highlights matching lines and lets the user jump forward/backward is a standard terminal emulator feature and would make Draxul's shell pane substantially more useful.

---

## Implementation Plan

- [ ] Read `libs/draxul-host/include/draxul/scrollback_buffer.h` and its implementation to understand the stored row format and existing API.
- [ ] Read how scrollback mode is currently entered and displayed (keyboard shortcut, viewport offset, etc.).
- [ ] Design the search interaction:
  - Scrollback mode is activated (existing behavior).
  - User types `/pattern` (or a dedicated keybinding opens a search mini-bar at the bottom of the pane).
  - Matching rows are highlighted in the scrollback view (distinct background color).
  - `n` / `N` jump to next/previous match.
  - `Escape` clears the search and returns to normal scrollback browsing.
- [ ] Implementation steps:
  - [ ] Add a `search(std::string_view pattern)` method to `ScrollbackBuffer` that returns a list of matching row indices (simple substring or regex match; regex recommended for extensibility).
  - [ ] Add state to the terminal host for current search pattern and current match index.
  - [ ] In the scrollback render path, highlight cells on matching rows with a search-highlight color.
  - [ ] Wire keybindings (`/`, `n`, `N`) in scrollback mode.
  - [ ] Add a `search_highlight` color to `AppConfig` with a sensible default.
- [ ] Consider using a subagent to implement the `ScrollbackBuffer::search()` method and the render highlight pass once the design is agreed.
- [ ] Build and run smoke test; manually test search with a few patterns.

---

## Acceptance

- In scrollback mode, typing `/foo` highlights all rows containing "foo".
- `n` / `N` jump between matches; the viewport scrolls to keep the current match visible.
- `Escape` clears the search state.
- Scrollback behavior without search is unchanged.

---

## Interdependencies

- No upstream blockers; self-contained.
- Icebox: configurable-scrollback-capacity (34) — independent; can be done in any order.

---

*claude-sonnet-4-6*
