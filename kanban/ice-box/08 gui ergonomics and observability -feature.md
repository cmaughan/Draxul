


# 08 GUI Ergonomics And Observability

## Why This Exists

These are not correctness blockers, but both reviews surfaced useful medium-priority improvements that would help daily use and future debugging.

Examples raised across the reviews:

- performance/debug overlay
- configurable padding
- better font discovery/selection UX
- tab/file chrome or other richer GUI affordances
- clickable paths/URLs and drag-drop style workflows

## Goal

Collect the next tier of GUI improvements without letting them displace the harder correctness work above.

## Implementation Plan

- [x] Start with observability.
  - [x] debug overlay for DPI, atlas usage, dirty counts, and frame timing
- [ ] Add low-risk ergonomics next.
  - [ ] configurable padding
  - [ ] improved font discovery/picking workflow
- [ ] Only then take on larger UI affordances.
  - [ ] tab/file chrome
  - [ ] clickable paths
  - [ ] drag/drop or dialogs

## Tests

- targeted UI snapshot additions for anything visible
- basic config round-trip coverage for new settings

## Suggested Slice Order

- [x] debug/perf overlay
- [ ] padding/font UX
- [ ] larger GUI affordances

## Sub-Agent Split

- one agent on observability
- one agent on settings/config UX
- defer larger affordances until the higher-priority backlog is complete
