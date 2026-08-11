# 20 searchable-scrollback -feature

**Priority:** LOW
**Type:** Feature (quality-of-life, terminal usability)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

The server retains semantic terminal scrollback and `RemoteTerminalHost` owns
each client's independent scroll position and selection, but there is no way to
search within retained rows. A pattern search should query paged server
scrollback while keeping the pattern, matches, highlighting, and navigation
strictly client-local.

---

## Implementation Plan

- [ ] Read `libs/draxul-terminal-core/include/draxul/scrollback_buffer.h` and its implementation to understand the stored row format and existing API.
- [ ] Read how scrollback mode is currently entered and displayed (keyboard shortcut, viewport offset, etc.).
- [ ] Design the search interaction:
  - Scrollback mode is activated (existing behavior).
  - User types `/pattern` (or a dedicated keybinding opens a search mini-bar at the bottom of the pane).
  - Matching rows are highlighted in the scrollback view (distinct background color).
  - `n` / `N` jump to next/previous match.
  - `Escape` clears the search and returns to normal scrollback browsing.
- [ ] Implementation steps:
  - [ ] Define a bounded paged-search/read strategy over the existing versioned
        server scrollback API; do not copy the entire retained history on every edit.
  - [ ] Add current pattern and match navigation state to `RemoteTerminalHost`.
  - [ ] Highlight matching cells in the client scrollback projection.
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

- Depends on the existing server scrollback paging capability; no local shell
  backend should be introduced.
- Existing configurable scrollback capacity should remain unchanged by search.

---

*claude-sonnet-4-6*
