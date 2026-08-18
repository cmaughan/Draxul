# Startup performance characterization

**Type:** test
**Priority:** 35

Fixed wall-clock budgets are too noisy for shared CI and a canary cannot both be
non-blocking and fail the required build. Use the existing startup instrumentation as an
opt-in or nightly comparison.

- [ ] Emit machine-readable phase and total timing from `--smoke-test` when requested.
- [ ] Compare warm runs against a configurable platform/generator baseline with enough
      repetitions to report variance.
- [ ] Register an opt-in/nightly `perf` label; do not gate ordinary correctness CI on a
      universal 500 ms threshold.
- [ ] Print the slowest phase and retained result path for investigation.
