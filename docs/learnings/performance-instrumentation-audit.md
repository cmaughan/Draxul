# Performance instrumentation audit

## Scope and inventory

The July 2026 audit found 899 production `PERF_MEASURE()` scopes across 103
translation units before cleanup. The largest concentrations were MegaCity
(286), `draxul-host` (156), the app layer (127), and `draxul-renderer` (97).

Call frequency and diagnostic value matter more than the raw count:

- Retain lifecycle, frame, I/O, allocation/upload, synchronization, and batch
  spans. These identify work a user can feel and boundaries an owner can act on.
- Retain a nested span only when it separates two independently actionable
  costs. Otherwise it double-counts elapsed time and adds collector contention.
- Remove probes from accessors, setters, coordinate helpers, small state
  transitions, and leaf math already owned by a retained span.
- Prefer one stable span around a loop or batch over a span on every element.

The first focused cleanup removed 19 leaf probes from `IsometricCamera`. It
retains the world-framing and visible-ground-footprint spans, while matrix
accessors, setters, and nested update helpers are covered by their owning input
or frame spans. The production inventory is now 880 scopes across the same 103
files; no diagnostics-facing lifecycle or render-pass name was removed.

## Collector overhead

The disabled collector previously took a steady-clock timestamp and then a
mutex-backed enabled check for every scope. That paid synchronization and clock
cost even though no sample could be recorded. The disabled path now performs a
single atomic enabled check and does no clock, lock, allocation, or string work.

`tests/perf_timing_tests.cpp` contains the hidden `[.perf-benchmark]` case. It
measures the legacy disabled-path shape, the current disabled scope, and an
enabled scope. Run it explicitly in a Release build; it is excluded from normal
CTest discovery so routine test time is unchanged.

```powershell
./build/tests/Release/draxul-test-core.exe "[.perf-benchmark]" --benchmark-samples 100
```

Record the local before/after ratio with the validation result rather than
turning a machine-dependent nanosecond threshold into a flaky correctness test.
The collector correctness tests remain the acceptance gate for aggregation,
normalization, smoothing, call counts, and reset behavior.

On the Windows Debug integration build used for this audit (100 samples), the
legacy disabled-path shape averaged 108.507 ns, the new disabled scope averaged
30.468 ns, and an enabled scope averaged 128.742 ns. The disabled path is 3.56x
faster (71.9% lower mean overhead) on that run. These values are evidence for
the design choice, not performance assertions baked into CI.

## Sampling decision

No sampled or thread-local collection API was added. The audit identified the
always-on disabled fast path as the first-order problem, and removing its clock
and mutex is simpler and preserves exact enabled measurements. If enabled
profiling is later shown to be a bottleneck, add batching behind a benchmark
that includes worker-thread reports and frame-boundary flushing; do not hide
synchronization or silently drop samples in the existing exact macro.
