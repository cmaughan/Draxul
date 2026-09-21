# Clear orphaned wide-glyph continuations

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

Writing a wide glyph over the leader of an adjacent wide pair leaves the displaced pair’s far-side continuation cell orphaned.

**Trigger:** Store a wide glyph at columns 5–6 and then write another wide glyph at column 4.

## Investigation

- [x] Add tests for wide-over-wide writes from both directions and at row edges.
- [x] Verify dirty tracking for every cleared or converted cell.
- [x] Audit other cell mutation paths for split leader/continuation pairs.

## Fix strategy

- [x] Detect when the destination continuation column is currently a wide-glyph leader.
- [x] Clear and dirty that leader’s former continuation before overwriting it.
- [x] Normalize all locally affected wide-pair metadata after mutation.

**Implementation note (2026-09-21):** `set_cell` clears and dirties the
displaced leader and its far continuation before creating the replacement pair.
New tests cover both overlap directions and the right-hand row edge; focused
grid coverage passed on macOS (75 cases, 635 assertions). The broader aggregate
is currently blocked linking unrelated PCBView symbols, so full grid, Unicode,
observation, and snapshot validation remain pending.

## Acceptance criteria

- [ ] No cell has `double_width_cont` without an immediately preceding valid leader.
- [ ] Overlapping wide writes render and serialize the expected cells.
- [ ] Grid, Unicode, observation, and render-snapshot tests pass.
