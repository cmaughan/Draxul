# Print the displayed zoomed viewport

**Summary:** Print the full visible zoomed pane so its contents are not cut off.

**Priority:** P2  
**Source:** `app/app.cpp`

**Evidence and trigger:** B28; printing crops with the stored split rectangle rather than the separately displayed zoomed viewport.

- [x] **Investigate:** Compare split geometry, displayed host geometry, chrome offsets, and content hints.
- [x] **Fix:** Snapshot the displayed viewport and matching print hint for capture.
- [x] **Acceptance:** Printing zoomed left, right, top, and bottom panes captures their visible content with correct offsets; unzoomed printing remains correct.
- [x] **Validation:** Run core aggregate tests, relevant capture checks, and same-cache smoke.

## Implementation (2026-10-10)

`PaneManager::displayed_viewport()` applies the same zoom rectangle and App-owned
chrome/border transformation used to set the host viewport. Printing snapshots
that rectangle together with the same host's pane-relative content hint, rather
than using hidden split geometry. Hidden/missing panes have no displayed viewport.

The regression covers left/right/top/bottom zoom targets, content-corner pixels,
chrome insets, pane-relative content hints, hidden panes and restored unzoomed
geometry through the real PaneManager and RGBA crop boundary. Final aggregate
and same-cache startup validation are pending. The native print dialog remains
macOS-only as documented; the geometry and crop behavior are platform-neutral.

## Final Debug validation (2026-10-10)

- Core + SatView + ScoreView aggregate: all 62 CTest entries passed in 45.57s.
- Same-cache native Metal startup smoke passed; the Debug app was explicitly
  linked in that cache first (3.63s).
- Basic native Metal snapshot comparison passed (0.0548% changed pixels).
- Controlled client concurrency checks passed 5 epoch-only repeats plus 15
  paired epoch/pre-wait repeats; prior aggregate failures and their proven
  synchronization corrections are retained in the session validation notes.
- Windows uses the same platform-neutral policy/geometry paths and normal CI.
  No separate Windows manual gate is introduced for these fixes.
- Final Release startup confirmation passed (see completed validation below).

## Completed validation

Final aggregate: 62/62 passed (45.57s). Two earlier 62-entry passes took
53.08s and 52.01s and exposed the documented client test synchronization races;
the second also observed a 262ms/250ms checkpoint timing failure that passed
both the first and final aggregate. Focused concurrency diagnosis: 5 epoch
repeats plus 15 paired epoch/pre-wait repeats, all passed after corrections
(20.44s total tests). These repeats overlap aggregate coverage intentionally
while diagnosing failures.

Debug startup smoke passed (~1.2s); basic native Metal comparison passed
(~1.8s) and registered `draxul-render-basic-view` CTest passed (1.66s).
The comparison was repeated once to confirm the registered CTest gate.
Final Release build/configure passed (63.95s total: configure/generate 10.3s,
compile/link ~53.6s); Release isolated startup smoke passed (~0.7s). The first
custom Release runtime exceeded the Unix socket path limit; the standard short
isolated runner succeeded, and no failed helper remains. Debug's initial
aggregate compile took 271.07s after the existing Release cache was reconfigured;
warm aggregate builds and the explicit app link reused that cache. One initial
sandbox build was blocked by compiler-cache access before tests; the authorized
run used the existing compiler cache. Windows/remote CI was not run locally.

Detailed session logs: `/tmp/draxul-easy-cards-aggregate-pass.log`,
`/tmp/draxul-easy-cards-render-ctest.log`, and
`/tmp/draxul-easy-cards-release-smoke.log`.
