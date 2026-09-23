# Native file monitoring and Kanban automatic refresh

- [x] Add independent static `draxul-file-monitor` with native macOS, Windows, and Linux backends; no idle polling or UI dependencies.
- [x] Wire Kanban root/submodule board invalidations to debounced UI-thread reloads, preserving selected cards across unambiguous lane moves.
- [x] Keep manual reload, explicit failures, safe callback shutdown, and the previous board on scan failure.
- [x] Add native filesystem and Kanban integration coverage and document the API/limitations.
- [x] Validate macOS aggregate tests and same-cache startup smoke.
- [x] Validate Windows backend in cross-platform CI.

Scope: existing board roots. Discovering entirely new submodule boards or replacing
a watched root requires manual `r`. Rezonality migration is a separate slice.

## Local validation (2026-09-23)

- macOS native watcher: 3 cases / 35 assertions passed, 0.42 s. The sandbox
  denied FSEvents startup; rerun with approved native-service access passed.
- Initial configure/generate: 12.5 s + 3.6 s. Latest incremental Debug aggregate
  build: one `draxul-tests-core` target, 3.5 s, passed. Shared-cache contention
  caused deferred attempts; no competing build or alternate cache was started.
- Core aggregate: 25/25 CTest entries passed, 40.54 s. Repeated once after
  fixing a test-only `/tmp` vs `/private/tmp` path comparison (first run:
  24/25, 39.97 s). Includes 17 Kanban host cases / 121 assertions and the
  3 native watcher cases. Earlier focused watcher coverage overlaps this gate.
- Same-cache Debug startup smoke: passed; not separately timed.
- Same-cache `draxul-render-basic-view`: 1/1 passed, 1.61 s. There is no existing
  Kanban snapshot; real filesystem-to-host refresh is covered by the integration
  test instead. No renderer code/layout was changed.
- Final same-cache Release build/startup confirmation: passed, 199.38 s total;
  configure/generate 10.6 s + 2.5 s, with the remainder compilation/link and
  one smoke launch. The Debug-to-Release switch rebuilt dependencies as well
  as the `draxul` app target; this was the required final confirmation, not
  another unit-test pass.
- Windows runtime validation remains pending; this host has neither a working
  cross-compiler nor that operating system. No remote CI was dispatched for the
  uncommitted changes.
- The Linux backend remains an unvalidated target. Linux build, runtime, GUI,
  and CI work is centralized in
  `kanban/ice-box/124 linux-platform-bringup -feature.md` rather than used as a
  completion gate for this card.

## Windows validation evidence (2026-09-23)

- The Debug Rezonality aggregate passed all 38 registered CTest entries on
  Windows, including `draxul-tests-file-monitor-shard` (0.20 s), the core and
  Kanban integration suites, the dynamic Rezonality module, and all seven
  registered Vulkan render comparisons (214.37 s total).
- The same-cache Debug startup smoke passed in 21.98 s.
- This directly exercised the native Windows watcher through the same
  cross-platform aggregate used by CI; no polling fallback or alternate build
  cache was used.
