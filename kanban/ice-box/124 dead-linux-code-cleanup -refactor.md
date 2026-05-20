# WI 124 — Dead Linux Code Path Cleanup

**Type:** Refactor  
**Severity:** Low (silent bitrot risk; no Linux CI to catch divergence)  
**Source:** Claude review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`main.cpp` (approx L109–127) and `NvimProcess` contain `#ifdef __linux__` code paths. However:
- There is no Linux CMake preset
- There is no Linux CI runner
- There is no Metal or Vulkan renderer configured for Linux

These dead paths diverge silently from the rest of the codebase with every refactor. They give false confidence that Linux is supported when it is not. Claude flagged this: "Dead Linux code paths silently bitrotting."

---

## Investigation Steps

- [ ] `grep -rn "__linux__" app/ libs/ --include="*.cpp" --include="*.h"` — enumerate all Linux guards
- [ ] For each guard: determine if the code is functional, aspirational, or vestigial
- [ ] Check git history for when Linux paths were last verified to compile

---

## Fix Strategy

**Option A (clean):** Remove all `#ifdef __linux__` paths. Add a `#error "Linux is not supported"` static_assert or CMake `message(FATAL_ERROR)` if Linux is detected, to give an explicit error rather than compiling garbage.

**Option B (document):** If Linux support is a planned future goal, add a `DRAXUL_LINUX_SUPPORT` CMake flag (off by default) that gates compilation of the Linux paths, and add a CI stub that at least compiles with that flag on a Linux runner.

Either way, no silent dead code.

---

## Acceptance Criteria

- [ ] No unguarded `#ifdef __linux__` blocks that silently include untested code
- [ ] Either: Linux paths are removed, or: they are gated by an explicit opt-in CMake flag
- [ ] macOS and Windows CI unaffected
- [ ] CI green

---

## Interdependencies

- Prerequisite for any future Linux CI attempt.
- Stand-alone; no other work items depend on this.
- Subagent recommended: quick, bounded, mechanical cleanup task.
