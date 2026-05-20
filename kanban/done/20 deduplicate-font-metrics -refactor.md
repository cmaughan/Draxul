# 20 Deduplicate FontMetrics Definition

## Why This Exists

`FontMetrics` is defined in two places:
- `libs/draxul-font/include/draxul/font_metrics.h` (authoritative)
- `libs/draxul-types/include/draxul/font_metrics.h` (labelled "compatibility definition")

If either copy is updated without syncing the other, the divergence is silent at compile time if the two headers are not included in the same translation unit. This is an ODR (One Definition Rule) violation waiting to happen.

**Source:** Both header files above.
**Raised by:** Claude (primary), GPT (both call it a top-10 bad thing).

## Goal

Remove the duplicate `draxul-types` copy of `FontMetrics`. All consumers should use the definition from `draxul-font`.

## Implementation Plan

- [x] Read `libs/draxul-types/include/draxul/font_metrics.h` and `libs/draxul-font/include/draxul/font_metrics.h` to confirm they are identical (or identify any divergence).
- [x] Use `grep` to find all files that include `draxul-types` `font_metrics.h` (e.g., `#include <draxul/font_metrics.h>` or `"draxul-types/..."`) and list them.
- [x] For each consumer that uses the `draxul-types` copy:
  - Determine whether it already links `draxul-font`, or whether it needs to be updated to do so.
  - If the consumer should not depend on `draxul-font` (e.g., it is `draxul-renderer` which has a one-way dependency), reconsider: `FontMetrics` may belong in `draxul-types` and the `draxul-font` copy may be the one to remove. Resolve ownership based on the dependency graph in `CLAUDE.md`.
- [x] Delete the duplicate header and fix all `#include` references.
- [x] Update `CMakeLists.txt` in any library that now needs to link `draxul-font` (or `draxul-types`) to get the definition.
- [x] Run `clang-format` on touched files.
- [x] Run `ctest --test-dir build`.

## Notes

The dependency graph in CLAUDE.md shows:
```
draxul-types → draxul-font (types depends on nothing; font depends on types)
```
`draxul-renderer` depends on `draxul-types` but NOT on `draxul-font`. If `RendererState` uses
`FontMetrics`, it must either get it from `draxul-types` or the dependency must be added.
Audit this before deleting.

Audit result:
- The two `FontMetrics` definitions were identical.
- `rg` found only `draxul-font` consumers (`text_service.h` and `font_engine.h`), so no renderer or other types-only target needed the compatibility header.
- No `CMakeLists.txt` changes were required.

Implemented by deleting `libs/draxul-types/include/draxul/font_metrics.h` and making
`libs/draxul-font/include/draxul/text_service.h` include its local `font_metrics.h`
directly.

## Sub-Agent Split

Single agent. Grep-and-replace work; no logic changes.
