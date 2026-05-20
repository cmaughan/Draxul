# WI 110 — config-layer-renderer-dep-cleanup

**Type:** refactor
**Priority:** 7 (build boundary violation — config transitively pulls in GPU/window headers)
**Source:** review-consensus.md §5 [GPT]
**Produced by:** claude-sonnet-4-6

---

## Problem

`libs/draxul-config/CMakeLists.txt:15` (approximately) publicly links `draxul-renderer` and/or `draxul-window`. This means any target that links `draxul-config` transitively depends on GPU/window headers — making "config" a non-leaf dependency that is much heavier than its API suggests.

Prior work items (WI 13 `appconfig-renderer-window-coupling`, WI 15 `appconfig-sdl-decoupling`) were filed and completed, but GPT's current-tree review indicates the CMakeLists still has public links that should be `PRIVATE` or removed entirely. This may be an incomplete fix or a regression.

---

## Investigation

- [x] Read `libs/draxul-config/CMakeLists.txt` in full — find all `target_link_libraries` calls and determine which are `PUBLIC` vs `PRIVATE`.
- [x] Check if any *public header* in `draxul-config/include/` actually includes renderer or window headers (which would justify a public link).
- [x] If the public headers are clean, change the link to `PRIVATE`. If a public header still includes renderer/window types, extract those types to avoid the dependency or move the problematic header.
- [x] Verify that existing users of `draxul-config` still compile without modification after the link is made private.

---

## Fix Strategy

- [x] Change any `PUBLIC` link of `draxul-renderer` / `draxul-window` in `draxul-config`'s CMakeLists to `PRIVATE`.
- [x] If the `draxul-config` public headers include renderer/window headers, move the relevant type forward-declarations or separate those types into a new narrow header.
- [x] Build the full project: `cmake --build build --target draxul draxul-tests`
- [x] Confirm no new include errors.
- [x] Run smoke: `py do.py smoke`

---

## Acceptance Criteria

- [x] `draxul-config` does not publicly expose `draxul-renderer` or `draxul-window` as transitive dependencies.
- [x] A target that links only `draxul-config` does not pull in GPU or windowing headers.
- [x] Full build succeeds; smoke test passes.

---

## Resolution

`app_options.h` was the culprit: it lived in `draxul-config/include/draxul/`
but actually held factory `std::function`s returning `IWindow` and
`RendererBundle`, which forced the public link of `draxul-renderer` and
`draxul-window` from `draxul-config`. Solution: relocate `app_options.h`
to `draxul-runtime-support` (which already publicly links `draxul-renderer`),
add `draxul-config` and `draxul-window` to runtime-support's public links,
remove renderer/window from `draxul-config`'s public links, and update
the four files that had been getting `AppOptions` transitively to include
`<draxul/app_options.h>` explicitly (`app/app.h`, `app/host_manager.cpp`,
`tests/host_manager_tests.cpp`, plus `tests/app_config_tests.cpp` for
`KeyEvent`/`HostKind` it had been getting via the same chain).

`draxul-config` is now leaf for the GPU/window subtrees (only links
`draxul-types` and `tomlplusplus` publicly).

---

## Interdependencies

- **WI 74** (megacity-private-build-includes -refactor, existing) — same class of CMake boundary violation; consider batching into one CMake cleanup pass.
