# 13 appconfig-renderer-window-coupling -refactor

**Type:** refactor
**Priority:** 13
**Source:** GPT review (review-latest.gpt.md) — HIGH finding

## Problem

`libs/draxul-app-support/include/draxul/app_config_types.h` is advertised as "struct definitions only" but includes:
- `renderer.h` (from `draxul-renderer`)
- `text_service.h` (from `draxul-font`)
- `window.h` (from `draxul-window`)

And it co-locates persistent `AppConfig` with runtime/test-only `AppOptions` factory seams.

The consequence: any translation unit that includes `app_config_types.h` for config struct access also drags in renderer, font, and window headers. A change to `renderer.h` triggers recompilation of all config consumers. This undermines the clean dependency layering the project otherwise enforces.

GPT rates this "High". It also blocks icebox 56 (live-config-reload) and icebox 37 (hierarchical-config), which need a clean config boundary to work correctly.

## Acceptance Criteria

- [x] Read `app_config_types.h` and `app_config.h` in full.
- [x] Identify which types in `app_config_types.h` genuinely need renderer/window/font headers (probably `RendererBundle*` references or similar).
- [x] Extract the pure data types (`AppConfig` fields, enums, simple structs) into a new header that includes only `<string>`, `<cstdint>`, `<optional>`, etc. — no library-level includes.
- [x] Move the types that require renderer/window/font headers to a separate "runtime config" or `AppOptions` header.
- [x] Update all `#include` sites to use the appropriate new header.
- [x] Verify `draxul-app-support` does NOT `target_link_libraries` against `draxul-renderer` or `draxul-window` (or that such links are intentional and documented).
- [x] Build both targets; run `ctest`.

## Implementation Notes

- This is a significant include-hygiene refactor. Read ALL files under `libs/draxul-app-support/include/` before proposing changes.
- Use `cmake --preset mac-debug` to verify the CMake dependency graph remains clean after the change.
- A sub-agent is a good fit: explore the full include chain, propose the split, then implement it.

## Interdependencies

- **Unblocks:** icebox 56 (live-config-reload), icebox 37 (hierarchical-config).
- No other blockers for this item itself.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
