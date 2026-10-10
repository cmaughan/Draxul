# Match shortcut chords to their starting prefix
**Summary:** Remember which shortcut prefix was pressed so the second key runs the intended command.

**Priority:** P2  
**Source:** `app/input_dispatcher.cpp`  
**Reported by:** Claude M5; consensus F48.

**Evidence and trigger:** Prefix activation at line 366 records no key/modifier identity; line 332 matches only the second key. Two prefixes sharing the same second key can execute the wrong binding.

- [x] **Investigate:** Trace prefix activation, normalization, timeout, cancellation, pane selection, and configuration reload.
- [x] **Fix:** Retain active prefix identity and require it to match during chord lookup.
- [x] **Acceptance:** `Ctrl+S, z` and `Ctrl+B, z` execute their respective commands regardless of binding order.
- [x] **Acceptance:** Timeout, cancellation, and unmatched chords preserve ordinary input behavior.
- [x] **Validation:** Run core aggregate tests and same-cache smoke.

## Implementation notes (2026-10-10)

- `InputDispatcher` retains an owning copy of the prefix `KeyEvent` and uses the existing `gui_prefix_matches` normalization when resolving the second key. Bindings with the same second key can no longer match another prefix's key or modifiers.
- Prefix identity is cleared on completion, unmatched/cancelled input, timeout, and reconfiguration. The pane-selection submode retains it across `prefix, 0`; focus changes continue to preserve the pending chord. Chord actions now copy their action string before execution because execution may reload binding storage.
- Added end-to-end coverage for Ctrl+S/Ctrl+B and Alt+S/Alt+B sharing `z`, both binding orders, one-sided Ctrl plus Caps Lock, a second key belonging only to another prefix, Escape forwarding, reconfiguration cancellation, and ordinary second-key forwarding after timeout. Existing modifier-only, keypad Enter, pane-selection, repeated-prefix and focus-change cases remain applicable.
- Aggregate and same-cache smoke are owned by the coordinating agent and remain pending here.

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
