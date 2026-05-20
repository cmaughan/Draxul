# 04 Grid Wide-Cell Repair And Redraw Robustness

## Why This Exists

The redraw path is much better than it used to be, but the remaining risk is now in edge semantics, especially around wide cells and hostile redraw input.

Current likely or reviewed issues:

- overwriting a continuation half may not fully repair the leader/continuation state
- redraw robustness coverage is still mostly happy-path fixture based
- malformed or partial event handling can be pushed further

## Goal

Make grid mutation rules and redraw parsing more hostile-input tolerant, especially around wide-cell edges.

## Implementation Plan

- [x] Verify and fix the wide-cell overwrite path.
  - [x] specifically test overwriting the continuation cell first
  - [x] repair both sides of the old wide-cell state
- [x] Expand redraw robustness fixtures.
  - [x] malformed arrays
  - [x] truncated `grid_line`
  - [x] odd repeat/empty combinations
- [x] Keep fixes local.
  - [x] prefer `Grid` or `UiEventHandler` fixes over broader pipeline churn

## Tests

- [x] direct `Grid` tests for continuation overwrite and scroll boundaries
- [x] redraw replay cases for malformed/truncated line batches
- [x] keep render snapshots green to catch visible fallout

## Suggested Slice Order

- [x] reproduce/lock down the wide-cell bug
- [x] patch `Grid`
- [x] add malformed redraw fixtures

## Sub-Agent Split

- one agent on `Grid`
- one agent on redraw fixture expansion
