# 16 hostmanager-dynamic-cast-removal -refactor

**Status:** Completed — the original `dynamic_cast<I3DHost*>` pattern is gone (see `plans/work-items-complete/14 app-rtti-capability-dispatch -refactor.md`). `app/host_manager.cpp` still has two MegaCityHost-specific casts for a different (non-3D-capability) reason; if those become problematic they should be tracked as a new item.

**Priority:** MEDIUM
**Type:** Refactor
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`app/host_manager.cpp:21–82` uses `dynamic_cast<I3DHost*>` after host initialisation to attach the 3D renderer. If a new host subclass forgets to implement `I3DHost`, the cast silently fails with no error, no log, and no compile-time enforcement. This is an invisible coupling that grows harder to debug as more host types are added.

---

## Implementation Plan

- [ ] Read `app/host_manager.cpp` in full.
- [ ] Read the `IHost` / `I3DHost` / `IGridHost` hierarchy in `libs/draxul-host/include/draxul/host.h`.
- [ ] Evaluate options:
  - **Option A (preferred)**: Make `attach_3d_renderer()` a virtual method on `IHost` with a default no-op implementation. Remove the `dynamic_cast` entirely. Hosts that want 3D rendering override the method.
  - **Option B**: Replace the `dynamic_cast` with a capability query method (`virtual bool wants_3d_renderer() const { return false; }`) and assert/log a WARN if 3D renderer attachment is attempted on a non-3D host.
  - **Option C**: Static-assert or use a template pattern so the compiler rejects non-3D host registration at the attach site.
- [ ] Implement the chosen option.
- [ ] Verify all existing host types (NvimHost, TerminalHost, MegaCityHost if present) compile and behave correctly.
- [ ] Run clang-format on touched files.
- [ ] Build and run smoke test + ctest.

---

## Acceptance

- Adding a new `IHost` subclass without implementing `I3DHost` produces either a compiler error or a clear runtime error/log — not a silent no-op.
- Existing 3D renderer attachment (for MegaCityHost or equivalent) still works.

---

## Interdependencies

- No upstream dependencies.
- If Megacity removal (icebox `17 megacity-removal-refactor`) is ever un-iceboxed, this refactor simplifies that work.
