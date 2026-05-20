# Refactor: Extract FontResolver from Header-Only to .cpp

**Type:** refactor
**Priority:** 18
**Source:** Gemini review

## Problem

`libs/draxul-font/src/font_resolver.h` is a large header that contains the full implementation of:
- Platform font path probing (system font directory traversal)
- Filesystem heuristics for locating bold/italic variants
- FreeType face loading
- HarfBuzz shaper setup

Because it is a header, this code is recompiled by every translation unit that includes it — which is currently `text_service.cpp` at minimum, but could fan out. It also cannot be unit-tested directly (no `.cpp` translation unit to link against) and is a merge-conflict magnet because all font work happens in one file.

## Investigation steps

- [x] Read `libs/draxul-font/src/font_resolver.h` — understand the full public interface (what functions/types are used by `text_service.cpp`?).
- [x] Check `libs/draxul-font/src/text_service.cpp` — how does it include and use `font_resolver.h`?
- [x] Check `libs/draxul-font/CMakeLists.txt` — is `font_resolver.h` listed as a source or just included?

## Implementation steps

- [x] Create `libs/draxul-font/src/font_resolver.cpp`.
- [x] Move all function **definitions** (implementations) from `font_resolver.h` to `font_resolver.cpp`.
- [x] Keep only declarations/inline structs in `font_resolver.h`.
- [x] Add `font_resolver.cpp` to `libs/draxul-font/CMakeLists.txt` sources.
- [x] Build and verify no compile errors.
- [x] Run `cmake --build build --target draxul draxul-tests` to confirm tests pass.
- [ ] Coordinate with `11 fontresolver-style-detection -test` — the tests require this extraction to be testable.

## Acceptance criteria

- [x] `font_resolver.h` contains only declarations (no function bodies > 3 lines).
- [x] `font_resolver.cpp` compiles as a standalone translation unit.
- [x] Build times for `text_service.cpp` are not affected by changes to the font resolver implementation.
- [x] Tests from `11 fontresolver-style-detection -test` can link against `font_resolver.cpp`.

## Interdependencies

- **`11 fontresolver-style-detection -test`**: do this refactor first so the resolver is unit-testable; write tests in the same agent pass.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
