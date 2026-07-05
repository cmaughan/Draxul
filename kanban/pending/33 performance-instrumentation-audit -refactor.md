# Audit performance instrumentation placement

**Type:** refactor
**Priority:** 33
**Raised by:** Claude

## Goal

`PERF_MEASURE()` appears on many trivial translation/parser helpers and acquires shared timing state when profiling, potentially distorting the work being measured and adding edit noise.

## Implementation plan

- [ ] Inventory probes by call frequency, measured body cost, aggregation value, and owning higher-level span.
- [ ] Benchmark profiling enabled/disabled on representative input/render workloads.
- [ ] Define guidance: keep lifecycle/frame/IO/batch spans; remove or sample trivial leaf probes; avoid nested spans that double-count without value.
- [ ] Add a low-overhead thread-local/sampled path only if measurements prove the current collector is the bottleneck.
- [ ] Remove low-value probes in focused subsystem commits, preserving names used by diagnostics where needed.
- [ ] Document the instrumentation policy near `perf_timing.h`.

## Tests and acceptance

- [ ] Preserve timing collector correctness/unit tests.
- [ ] Report before/after overhead and retained diagnostic coverage.
- [ ] No user-visible behavior change; full tests/smoke pass.

## Dependencies and parallelism

Independent. Good profiling sub-agent task, but require evidence before broad mechanical deletion.

<model>GPT-5 Codex</model>
