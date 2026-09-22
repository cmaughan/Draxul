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

**Implementation note (2026-09-22):** `set_cell` clears and dirties the
displaced leader and its far continuation before creating the replacement pair,
and normalizes edge writes that cannot fit a continuation. Bulk resize and
partial-scroll mutations now normalize the affected rows, including stationary
cells immediately outside a scroll rectangle when the other half of their pair
moves. Focused coverage verifies both overlap directions, row edges, dirty
updates, renderer clearing, and remote snapshot serialization. On macOS the
focused core build passed along with grid (73 cases, 614 assertions), remote
terminal protocol (4 cases, 17 assertions), semantic snapshot (4 cases, 40
assertions), Unicode (14 cases, 83 assertions), and observation scheduling (1
case, 9 assertions) filters. The Unicode filter required normal local test
socket access; its sandboxed attempt failed to bind and its permitted rerun
passed.
The final Debug validation passed all 45 unit CTest entries, the same-cache
startup smoke, and all five default Metal render comparisons.

## Acceptance criteria

- [x] No cell has `double_width_cont` without an immediately preceding valid leader.
- [x] Overlapping wide writes render and serialize the expected cells.
- [x] Grid, Unicode, observation, and render-snapshot tests pass.
