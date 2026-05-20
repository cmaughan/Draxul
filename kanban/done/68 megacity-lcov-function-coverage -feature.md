# 68 Megacity LCOV Function Coverage

*Filed by: codex — 2026-03-29*
*Source: user request / Megacity coverage planning*

## Problem

MegaCity currently has two live overlays:

- `Perf`, driven by runtime timing heat from `PERF_MEASURE()`
- `Coverage`, which is really runtime "touched" coverage derived from the same live perf stream

That is useful for interactive profiling, but it is not true test/build coverage. The repo
already supports LLVM coverage builds and exports `build/coverage.lcov`, but Megacity has no way
to import that report and show which semantic functions were covered by the test suite.

For the current goal, line-accurate coverage is not required. Function coverage is enough.

## Goal

Add a new Megacity coverage path that imports an `lcov` tracefile and lights semantic function
layers based on whether they were covered in the report.

This should stay separate from the existing live runtime coverage mode.

## Acceptance Criteria

- [x] Megacity can load an `lcov` report from disk, starting with a simple path such as
      `build/coverage.lcov` or a user-configured override.
- [x] A new overlay mode exists for imported/report coverage and is distinct from runtime
      `Perf` and runtime `Coverage`.
- [x] Function coverage is mapped onto semantic building layers using existing semantic identity
      (`source_file_path`, owning qualified name, function name), with sensible fallback matching
      when exact names differ slightly.
- [x] Covered functions render as hot/visible in the city; uncovered functions remain at base
      color.
- [x] The Megacity debug panel reports useful coverage-import diagnostics:
      total report functions, matched functions, unmatched functions, heated layers/buildings.
- [x] Tooltip/debug text for hovered functions indicates whether the function is covered by the
      imported report.
- [x] Existing runtime perf/coverage modes keep working unchanged.
- [x] `docs/features.md` is updated when the feature lands.

## Implementation Plan

1. Read the existing LLVM/lcov generation path in:
   - `CMakeLists.txt`
   - `CMakePresets.json`
   - `.github/workflows/coverage.yml`
2. Add a small `lcov` parser that extracts function coverage from:
   - `SF:` source file
   - `FN:` function declaration
   - `FNDA:` function hit count
3. Define a separate imported-coverage snapshot/model rather than overloading
   `RuntimePerfSnapshot`.
4. Extend Megacity overlay configuration to add a distinct report-coverage mode.
5. Map imported function coverage onto semantic layers using:
   - exact `file + owner + function`
   - fallback `owner + function`
   - fallback `file + function`
6. Reuse the existing per-layer scene heat path so imported coverage drives the same shader
   overlay without changing the rendering model.
7. Add debug readout for matched/unmatched imported coverage entries.
8. Add focused tests for:
   - `lcov` parsing
   - semantic function matching
   - Megacity overlay state/debug counters
9. Manual validation:
   - generate `build/coverage.lcov`
   - load it in Megacity
   - confirm known covered modules/buildings light up

## Files Likely Touched

- `libs/draxul-megacity/src/live_city_metrics.h`
- `libs/draxul-megacity/src/live_city_metrics.cpp`
- `libs/draxul-megacity/include/draxul/megacity_code_config.h`
- `libs/draxul-megacity/src/megacity_code_config.cpp`
- `libs/draxul-megacity/src/megacity_host.cpp`
- `libs/draxul-megacity/src/ui_treesitter_panel.cpp`
- `libs/draxul-types` or a new small parser utility if shared placement makes more sense
- `tests/megacity_scene_tests.cpp`
- new targeted parser tests
- `docs/features.md`

## Notes

- Prefer importing `lcov` rather than making Megacity understand LLVM `.profdata` directly.
  The repo can continue generating coverage with LLVM and exporting `lcov` as an interchange
  format.
- Function-level coverage is sufficient for the first version. Do not block this item on adding
  line-range metadata to semantic layers.
- If function name matching proves too weak, a later follow-up can add source line ranges to
  semantic layers and map line hits more precisely.

## Interdependencies

- Depends on the existing live per-layer Megacity heat path, which is already present.
- Independent of the runtime perf collector; should not change or regress the existing live
  profiling workflow.
