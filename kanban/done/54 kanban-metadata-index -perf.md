# Index Kanban metadata filenames during ordering

**Summary:** Look up saved card positions directly so restoring a large Kanban lane does not repeatedly search every card.

**Source:** `modules/kanban/draxul-kanban/src/kanban_store.cpp`  
**Priority:** P2; static, high confidence. **Reported by:** Codex. Lines 228–252 search the complete card vector for each saved filename, approaching N² comparisons for a fully listed lane. The separate GUI-worker change moves this CPU work but does not remove it.

- [x] **Baseline:** Count comparisons and reload CPU for 1,000, 5,000, and 10,000 cards in varied orders.
- [x] **Implement:** Build a temporary owning filename-to-index map before moving cards.
- [x] **Functional safety:** Preserve stale and duplicate metadata names and original order of unmatched cards; avoid keys into moved strings.
- [x] **Compare:** Require lookup growth proportional to card count and identical final order.
- [x] **Platforms:** Check ordering on Windows and macOS, aggregate and smoke.
- [x] **Acceptance:** Metadata ordering no longer repeatedly scans the lane.

## Implementation notes (2026-10-10)

- Build a reserved `unordered_map<string, size_t>` before moving cards. Keys own filenames; no views reference moved strings. Each saved name performs one lookup, and the existing `used` vector prevents moving a card twice when metadata repeats its name. Stale names remain ignored and unmatched cards retain their original order.
- Added a store boundary regression with stale and duplicate metadata, two unmatched cards, and verification that reordered card paths still identify the correct contents. Aggregate execution remains pending with the coordinating agent.
- Standalone optimized macOS benchmark extracted the exact baseline and updated `apply_card_metadata` functions with real `KanbanCard` types, compiled with `clang++ -O2 -std=c++20`. Timings below are the median of seven runs, exclude fixture copies, and measure ordering CPU work rather than disk reload. An instrumented baseline separately counted filename comparisons. Forward/reverse/shuffle (MT19937 seed 42) produced identical final orders in every size.

| Cards | Baseline comparisons | Indexed insertions + lookups | Forward before/after (ms) | Reverse before/after (ms) | Shuffle before/after (ms) |
|---|---:|---:|---:|---:|---:|
| 1,000 | 500,500 | 1,000 + 1,000 | 0.779 / 0.109 | 0.702 / 0.043 | 0.526 / 0.037 |
| 5,000 | 12,502,500 | 5,000 + 5,000 | 7.187 / 0.209 | 16.300 / 0.217 | 13.125 / 0.227 |
| 10,000 | 50,005,000 | 10,000 + 10,000 | 28.367 / 0.448 | 67.042 / 0.450 | 58.153 / 0.488 |

- Measurements show linear map operations for these fixtures. Hash tables retain their standard collision-dependent worst case; no worst-case complexity claim is made. Benchmark sources are `/tmp/draxul-kanban-order-benchmark.py` and `/tmp/draxul-kanban-order-benchmark.cpp` in the current session.
- Reviewed the shared Windows/macOS path: lookup keys remain owning UTF-8 filenames and do not alter native filesystem path conversion. Existing UTF-8 store tests are included in the coordinating aggregate. Windows execution follows normal CI; no separate manual Windows gate is required for this platform-neutral change.

### Full reload process CPU measurement

Compiled the actual baseline store from core `a2234cda` and the updated store, each with the actual `kanban_board.cpp` and a separate driver (`clang++ -O2 -std=c++20`, macOS arm64). The driver creates real Markdown files and metadata, warms one load, then takes the median process CPU (`std::clock`) of five `load_kanban_board` calls. Every load verifies all final filenames against the requested order. These separate baseline/current passes ran while other work was active; they are not a controlled end-to-end speedup measurement. The full reload results did **not** show a speedup, despite the isolated ordering improvement above. A quiet paired reload comparison was subsequently run after aggregate compilation stopped, as recorded below.

| Cards | Forward before/after CPU (ms) | Reverse before/after CPU (ms) | Shuffle before/after CPU (ms) |
|---|---:|---:|---:|
| 1,000 | 28.074 / 57.537 | 29.074 / 57.922 | 27.833 / 60.116 |
| 5,000 | 159.297 / 319.419 | 164.640 / 286.822 | 160.608 / 324.112 |
| 10,000 | 270.859 / 652.734 | 351.202 / 622.453 | 459.526 / 648.211 |

Driver/source artifacts for this session: `/tmp/draxul-kanban-reload-benchmark.cpp`, `/tmp/draxul-kanban-store-baseline.cpp`; binaries `/tmp/draxul-kanban-reload-baseline` and `/tmp/draxul-kanban-reload-indexed`. Build fixtures were removed after measurement.

### Paired full reload comparison after compilation stopped

A fixed shared 10,000-card shuffled fixture (MT19937 seed 42) was loaded by alternating baseline and indexed binaries. One warm pair was excluded; each of the three measured pairs verified the complete resulting order. Drivers compile the same actual store/board sources and use `std::clock` to measure process CPU for the full load.

| Pair | Baseline CPU (ms) | Indexed CPU (ms) |
|---|---:|---:|
| 1 | 350.354 | 272.490 |
| 2 | 351.507 | 297.601 |
| 3 | 447.921 | 274.672 |
| Median | 351.507 | 274.672 |

The paired fixture's median full reload CPU decreased by 21.9%; this is a single local 10k shuffled fixture result, not a claim for every board or filesystem. The stronger mechanism evidence remains the N insertions/N lookups and large isolated ordering improvement at every measured size/order.

Session artifacts: `/tmp/draxul-kanban-paired-reload.cpp`, `/tmp/draxul-kanban-paired-baseline`, `/tmp/draxul-kanban-paired-indexed`; shared fixture `/tmp/draxul-kanban-paired-fixture`. Invocation for either binary: `<binary> /tmp/draxul-kanban-paired-fixture load`. The fixture is temporary and may be removed after validation.

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
