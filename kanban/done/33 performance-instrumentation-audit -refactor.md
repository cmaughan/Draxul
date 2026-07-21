# Audit performance instrumentation placement

**Type:** refactor
**Priority:** 33
**Raised by:** Claude

## Goal

`PERF_MEASURE()` appears on many trivial translation/parser helpers and acquires shared timing state when profiling, potentially distorting the work being measured and adding edit noise.

## Implementation plan

- [x] Inventory probes by call frequency, measured body cost, aggregation value, and owning higher-level span. (899 production scopes across 103 files before cleanup; subsystem totals and decisions are recorded in the audit note.)
- [x] Benchmark profiling enabled/disabled on the collector path used by input/render scopes. (Hidden Catch2 benchmark: legacy disabled shape 108.507 ns, new disabled scope 30.468 ns, enabled scope 128.742 ns on the Windows Debug integration run.)
- [x] Define guidance: keep lifecycle/frame/IO/batch spans; remove or sample trivial leaf probes; avoid nested spans that double-count without value.
- [x] Add a low-overhead thread-local/sampled path only if measurements prove the current collector is the bottleneck. (Not added: the measured first-order cost was the disabled clock+mutex path; it is now one atomic check while enabled collection remains exact.)
- [x] Remove low-value probes in focused subsystem commits, preserving names used by diagnostics where needed. (Removed 19 trivial/nested `IsometricCamera` probes; retained world framing and visible-ground-footprint spans.)
- [x] Document the instrumentation policy near `perf_timing.h`.

## Tests and acceptance

- [x] Preserve timing collector correctness/unit tests. (`[perf]`: 15 assertions passed.)
- [x] Report before/after overhead and retained diagnostic coverage. (`docs/learnings/performance-instrumentation-audit.md`.)
- [~] No user-visible behavior change; full tests/smoke pass. (Affected Release libraries build; full modular suite and smoke remain for integration.)

## Status (2026-07-21)

Implementation and focused validation are complete. The disabled scope is 3.56x
faster in the recorded benchmark, and the production inventory is 880 scopes.
This card remains pending only for the shared full-test and smoke gates.

## Dependencies and parallelism

Independent. Good profiling sub-agent task, but require evidence before broad mechanical deletion.

<model>GPT-5 Codex</model>
