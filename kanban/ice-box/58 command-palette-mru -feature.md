# WI 58 — command-palette-mru

**Type**: feature  
**Priority**: 10 (natural complement to the delivered command palette)
**Source**: review-consensus.md §F2 [P]  
**Produced by**: claude-sonnet-4-6

---

## Goal

Add most-recently-used (MRU) sorting to the command palette so that recently executed actions bubble to the top of the list, reducing keystrokes for repeat actions.

---

## Pre-condition

The command palette is implemented; this card adds ranking/history only.

---

## Background

The command palette (`app/command_palette_host.cpp`) currently shows all actions in a static order. MRU is a session-local list (not persisted across restarts) of the last N actions executed, displayed above the full sorted list. This is the standard behaviour for VSCode, IntelliJ, and other command palettes.

---

## Tasks

- [ ] Read `app/command_palette_host.cpp` and `app/command_palette_host.h` — understand the current action list data flow: where actions are fetched, how they are sorted, how selection works.
- [ ] Design the MRU store: a `std::deque<std::string>` of action names, max 10 entries, deduplication (moving an already-present action to the front rather than adding a duplicate). This is session-local (not persisted).
- [ ] When an action is executed via the palette, push it to the front of the MRU deque.
- [ ] When the palette opens, display MRU entries (if any) at the top of the list, separated visually from the full alphabetical list (e.g., a "Recent" header or a blank line separator using ImGui `Separator()`).
- [ ] Filtering/search applies to both the MRU section and the full list simultaneously; MRU entries that do not match the filter are hidden.
- [ ] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`

---

## Acceptance Criteria

- Opening the palette after executing an action shows that action at the top of the list.
- Repeated execution of the same action does not add duplicates; the entry moves to the front.
- Filtering hides MRU entries that do not match.
- The MRU list is cleared when the app restarts (session-local).
- Smoke test passes.

---

## Interdependencies

- No remaining prerequisite; keep persistence scoped to non-sensitive action identifiers.

---

## Notes for Agent

- Keep the MRU store inside `CommandPaletteHost`; do not add it to `App` or `PaneManager`.
- Max MRU size of 10 is a reasonable default; do not expose it as a config option yet.
- The visual separator between "Recent" and "All" should be subtle — a dim label, not a prominent divider.
