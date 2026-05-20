# Bug: Unsigned underflow in ScopedPerfMeasure corrupts timing averages

**Severity**: HIGH
**File**: `libs/draxul-types/src/perf_timing.cpp:383`
**Source**: review-bugs-consensus.md (H1)

## Description

`ScopedPerfMeasure::~ScopedPerfMeasure()` computes:
```cpp
runtime_perf_collector().report_timing(*tag_, end_microseconds - start_microseconds_);
```
Both values are `uint64_t`. If a clock discontinuity occurs (NTP step backward, TSC CPU migration), `end_microseconds < start_microseconds_` and the subtraction wraps to approximately `UINT64_MAX` (~1.8×10¹⁹ µs ≈ 584,542 years). The zero-guard in `report_timing` (`microseconds == 0`) does not catch this. The enormous value enters the EMA and `smoothed_frame_fraction` stays at ~1.0 for an extended period, making the perf heat display permanently incorrect until it slowly decays.

## Trigger Scenario

NTP backward step or process migration across TSC-divergent CPU cores (NUMA systems) while `ScopedPerfMeasure` is active.

## Fix Strategy

- [x] Add a guard in the destructor:
  ```cpp
  if (end_microseconds > start_microseconds_)
      runtime_perf_collector().report_timing(*tag_, end_microseconds - start_microseconds_);
  ```
- [x] Optionally add a maximum clamp (e.g., 1 second) to prevent any single outlier from dominating the EMA

## Acceptance Criteria

- [x] Injecting `end_microseconds = start_microseconds_ - 1` in a unit test does not result in a report to the collector
- [x] Normal timing measurements continue to be reported correctly
- [x] Existing `perf_timing_tests.cpp` continue to pass
