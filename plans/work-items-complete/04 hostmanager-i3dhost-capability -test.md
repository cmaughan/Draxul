# 04 hostmanager-i3dhost-capability -test

**Type:** test
**Priority:** 4
**Source:** GPT review (review-latest.gpt.md); Claude flags the silent-null-cast risk

## Problem

`host_manager_tests.cpp` only creates plain `IHost` instances. There is no test for:
- What happens when an `I3DHost` is registered — does `attach_3d_renderer()` get called exactly once?
- Does a host that is NOT `I3DHost` correctly receive no renderer attachment?
- What happens with mixed host types in the same `HostManager`?

This matters because `HostManager` uses `dynamic_cast<I3DHost*>` silently (icebox 16). Without a test that exercises the `I3DHost` path, a regression in this attachment logic would be invisible until runtime.

## Acceptance Criteria

- [x] Read `tests/host_manager_tests.cpp` and `libs/draxul-host/src/host_manager.cpp` (or wherever HostManager lives).
- [x] Add a fake `I3DHost` subclass with a counter or flag for `attach_3d_renderer()`.
- [x] Add tests:
  - [x] `I3DHost` registered with `HostManager` → `attach_3d_renderer()` called exactly once.
  - [x] Plain `IHost` registered → `attach_3d_renderer()` NOT called.
  - [x] Mixed registration (one `I3DHost`, one plain `IHost`) → correct selective attachment.
  - [x] `I3DHost` still receives `attach_3d_renderer()` even when added after another plain host.
- [x] Run under `ctest` and `mac-asan`.

## Implementation Notes

- This test validates the current `dynamic_cast`-based behaviour without fixing it (that is icebox 16). The goal is a regression safety net.
- Keep the fake `I3DHost` minimal — one `bool attached = false` flag checked after `HostManager` initialization is sufficient.
- A sub-agent can do this: read the manager source and existing tests, then add the new cases.

## Interdependencies

- No blockers.
- Provides safety net for when icebox 16 (`hostmanager-dynamic-cast-removal`) is eventually implemented.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
