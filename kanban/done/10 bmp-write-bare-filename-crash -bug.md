# WI 10 — `write_bmp_rgba` throws `filesystem_error` on bare filenames

**Type:** bug  
**Severity:** MEDIUM  
**Source:** review-bugs-latest.gpt.md (HIGH)  
**Consensus:** review-consensus.md Phase 4

---

## Problem

`libs/draxul-types/src/bmp.cpp:46`:

```cpp
std::filesystem::create_directories(path.parent_path());
```

This is called **unconditionally**. When the caller passes a bare filename without a directory component (e.g. `--screenshot out.bmp` or `--render-test out.bmp`), `path.parent_path()` returns an empty path. Many standard library implementations (libc++, MSVC STL) throw `std::filesystem::filesystem_error` for `create_directories("")`, which propagates uncaught and terminates the process.

**Trigger:** `./draxul --screenshot out.bmp` or any render-test capture path without an explicit directory component.

**Files:**
- `libs/draxul-types/src/bmp.cpp` (~line 46)

---

## Implementation Plan

- [x] Guard the call:
  ```cpp
  const auto parent = path.parent_path();
  if (!parent.empty()) {
      std::filesystem::create_directories(parent);
  }
  ```
  (Guard was already in place from WI 106 at `libs/draxul-types/src/bmp.cpp:46–48`; WI 10 is a duplicate report filed before the existing fix was noticed.)
- [x] Add a unit test that calls `write_bmp_rgba` with a bare filename in the current directory and asserts it writes successfully without throwing. (See `tests/bmp_write_tests.cpp` — covers both the bare-filename case and the nested-parent-directory case.)
- [x] Verify the fix on macOS. (`draxul-tests [bmp]` passes under `mac-debug`; smoke test also passes. Windows path uses the same guarded code and is exercised by the same unit tests in CI.)

---

## Interdependencies

- Fully self-contained. No dependencies on Phase 1 bugs.
- The render test infrastructure (`do.py smoke`, render-test scenarios) exercises this path; fix before any new render-test scenarios are added.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
