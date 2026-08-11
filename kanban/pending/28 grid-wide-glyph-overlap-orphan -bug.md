# Clear orphaned wide-glyph continuations

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

Writing a wide glyph over the leader of an adjacent wide pair leaves the displaced pair’s far-side continuation cell orphaned.

**Trigger:** Store a wide glyph at columns 5–6 and then write another wide glyph at column 4.

## Investigation

- [ ] Add tests for wide-over-wide writes from both directions and at row edges.
- [ ] Verify dirty tracking for every cleared or converted cell.
- [ ] Audit other cell mutation paths for split leader/continuation pairs.

## Fix strategy

- [ ] Detect when the destination continuation column is currently a wide-glyph leader.
- [ ] Clear and dirty that leader’s former continuation before overwriting it.
- [ ] Normalize all locally affected wide-pair metadata after mutation.

## Acceptance criteria

- [ ] No cell has `double_width_cont` without an immediately preceding valid leader.
- [ ] Overlapping wide writes render and serialize the expected cells.
- [ ] Grid, Unicode, observation, and render-snapshot tests pass.
