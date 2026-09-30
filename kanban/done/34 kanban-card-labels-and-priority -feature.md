# Simplify Kanban card labels and surface priority

**Priority:** P2  
**Raised by:** user, 2026-09-30

## Goal

Show concise card names with a compact `[#number]` prefix but without the
numeric prefix in the title, type suffix, or
`.md` extension. Place a single priority symbol beside the type icon when a card
declares `**Priority:** Pn`, and show higher-priority cards first in each lane.

## Tasks

- [x] Read card priority from Markdown and preserve exact filenames for storage, opening, and moves.
- [x] Add display-only label cleanup, a distinct test-card icon, and priority glyphs.
- [x] Sort the visible aggregate lane by priority while preserving saved order within each rank.
- [x] Keep manual reordering within the selected source and priority group.
- [x] Add tests for filename cleanup, priority parsing, and stable ordering.
- [x] Document the visible behavior.
- [x] Run the Debug core aggregate, same-cache smoke, and a relevant render check.

## Notes

Sorting only the visible board avoids changing each source board's persisted
order or the source-group assumptions used by lane moves. No-priority cards
remain after ranked cards. The original filenames remain the card identity.

The first screenshot exposed a colored background behind priority glyphs. The
priority highlights now use the card row background. A fresh 1400×850 Mac
capture at `/tmp/draxul-kanban-priority-final.png` confirms the symbols are
clear, the strip is gone, sequence numbers remain visible, and P1 cards lead
their lanes. There is no Kanban-specific render snapshot scenario yet.

Debug Kanban core tests passed: 306 assertions in 41 cases. Kanban host tests
passed: 162 assertions in 20 cases. The final Debug core aggregate passed all
55 CTest entries in 35.66 seconds, followed by a successful same-cache Debug
startup smoke with Metal initialized. A concurrent app CLI extraction briefly
blocked the aggregate; the final run passed after its source settled.
