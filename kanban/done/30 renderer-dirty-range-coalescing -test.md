# Renderer dirty-range contract coverage

**Type:** test
**Disposition:** Superseded by the actual minimal-enclosing-range contract.

`tests/renderer_state_tests.cpp` verifies that disjoint base-grid writes produce one
minimal enclosing `[begin,end)` upload range and that overlay dirtiness remains
separate. The original card assumed a range-list algorithm and cursor base-cell
dirtiness that the renderer does not use.
