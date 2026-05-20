# 13 textinputevent-lifetime-safety -refactor

**Priority:** MEDIUM
**Type:** Refactor (latent use-after-free safety)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`TextInputEvent` stores a raw `const char*` pointing into SDL's internal event buffer. The lifetime guarantee — valid only until the next SDL event is polled — is implicit and undocumented in the struct. If any code path stores the event and processes it one frame later (e.g., during an async host dispatch), it would produce a silent use-after-free. The struct needs to either own its data or clearly document the lifetime contract.

---

## Implementation Plan

- [x] Search for `TextInputEvent` in `libs/draxul-types/include/draxul/events.h` and all usage sites.
- [x] Evaluate the two options:
  - **Option A (preferred):** Change `const char* text` to `std::string text` in `TextInputEvent`. All construction sites (SDL event handling) copy the string once at the boundary. All consumer sites are unchanged.
  - **Option B:** Add a lifetime comment to the struct header and add a `static_assert` or `[[nodiscard]]` decoration that makes delayed use of the pointer harder to miss.
- [x] Implement Option A (ownership is cleaner than documentation for a safety issue at a system boundary).
  - [x] Update `TextInputEvent` struct.
  - [x] Update all event construction sites (likely in `libs/draxul-window/src/sdl_window.cpp`).
  - [x] Update any consumer sites that held `const char*` to use `.c_str()` or `std::string_view` as needed.
- [x] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [x] Run `clang-format` on all modified files.

---

## Acceptance

- `TextInputEvent` owns its string data.
- No raw pointer into SDL's internal buffer is retained beyond the event handler scope.
- All tests pass; no behavior change.

---

## Interdependencies

- No upstream blockers; self-contained.

---

*claude-sonnet-4-6*
