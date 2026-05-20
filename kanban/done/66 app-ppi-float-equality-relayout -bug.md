# Bug: Float equality used for PPI change detection may miss relayout

**Severity**: MEDIUM
**File**: `app/app.cpp:~687`
**Source**: review-bugs-consensus.md (M7)

## Description

PPI change detection uses exact float equality:
```cpp
if (new_ppi == display_ppi_) return;
```
Floating-point representation differences between two computationally equivalent PPI values (e.g., from different code paths on the same nominal DPI) can cause the guard to pass when it should not, skipping a relayout and leaving the grid rendered at the wrong scale after a monitor change.

## Trigger Scenario

Moving the app window between monitors with the same nominal DPI but marginally different raw PPI computations (sub-pixel rounding differences, driver quirks).

## Fix Strategy

- [x] Replace the exact comparison with an epsilon check:
  ```cpp
  if (std::abs(new_ppi - display_ppi_) < 0.5f) return;
  ```
  0.5 DPI is imperceptible; any real change will exceed it.

## Acceptance Criteria

- [x] Moving between monitors that differ by < 0.5 DPI does not trigger unnecessary relayout
- [x] Moving between monitors with a measurable DPI difference correctly triggers relayout
- [x] No regression in existing DPI-handling tests or render snapshots
