# 14 for-each-host-template -refactor

**Priority:** LOW
**Type:** Refactor (per-frame performance, zero-cost abstraction)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`HostManager::for_each_host()` accepts a `std::function<void(LeafId, IHost&)>` callback. `std::function` involves a heap allocation for the closure object on every call. This function is called per-frame for each host (pumping, viewport updates, dirty checks). Replacing with a function template makes the callback zero-cost and is a straightforward mechanical change.

---

## Implementation Plan

- [ ] Find `HostManager::for_each_host()` declaration in `app/host_manager.h`.
- [ ] Change the signature from:
  ```cpp
  void for_each_host(std::function<void(LeafId, IHost&)> fn);
  ```
  to:
  ```cpp
  template<typename F>
  void for_each_host(F&& fn);
  ```
- [ ] Move the implementation to the header (templates must be defined where they are used, unless explicit instantiation is used).
- [ ] Check all call sites — the lambdas passed should work unchanged since the signature is compatible.
- [ ] Remove the `#include <functional>` from the header if `std::function` is no longer used there.
- [ ] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [ ] Run `clang-format` on modified files.

---

## Acceptance

- `for_each_host()` is a function template.
- No `std::function` heap allocation occurs at the call sites (verified by inspection or profiler if desired).
- All tests pass; no behavior change.

---

## Interdependencies

- No upstream blockers; self-contained.

---

*claude-sonnet-4-6*
