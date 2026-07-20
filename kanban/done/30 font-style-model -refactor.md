# Unify the font style model

**Type:** refactor
**Priority:** 30
**Raised by:** Claude

## Goal

Replace repeated normal/bold/italic/bold-italic branches and parallel caches in `font_resolver.cpp`/`.h` with one indexed `FontStyle` model.

## Implementation plan

- [x] Define `enum class FontStyle { Regular, Bold, Italic, BoldItalic, Count }` plus constexpr trait helpers.
- [x] Replace parallel face/path/availability/cache members with `std::array` indexed by style.
- [x] Consolidate load, fallback, selection, and reset loops while retaining style-specific diagnostics.
- [x] Preserve public `TextStyle`/font APIs; keep FreeType/HarfBuzz types private.
- [x] Audit synthetic-style behavior and fallback precedence for each style.
- [x] Remove obsolete duplicated methods only after parity tests cover all four styles.

## Tests and acceptance

- [x] Table-test resolution/fallback/cache invalidation for every style and missing variant combination.
- [x] Verify config round-trip paths remain mapped correctly.
- [x] Render ligature/style references without blessing unrelated differences.
- [ ] Build/font tests/ctest/smoke pass on both platform paths. — macOS/Metal validated
      (build + ctest 11/11 + smoke all green); Windows/Vulkan validation pending CI.

## Dependencies and parallelism

Independent font refactor; avoid overlap with active text-atlas changes until they settle.

## Status — 2026-07-19

Refactor complete on macOS/Metal. The four style slots live in one
`std::array<StyledFont, FONT_STYLE_COUNT>` on `FontResolver`, the fifteen
duplicated per-style accessors are gone, and every consumer (`FontSelector`,
`LigatureAnalyser`, `font_tests.cpp`) now uses the indexed
`style(FontStyle)` / `has_style(FontStyle)` API. `FontSelector` keeps one
cache slot per style in a `std::array<StyleCache, FONT_STYLE_COUNT>`.
Behavior preserved: warning text/order, the BoldItalic→Bold→Italic→regular
fallback precedence, and the same-as-regular skip are all locked in by table
tests (`font_style_model_tests.cpp`, `font_resolver_style_tests.cpp` including
a new per-style cache-isolation/reset test).

Validation: `cmake --build build --target draxul draxul-tests` OK;
`ctest --test-dir build` 11/11 pass (5 render + 4 unit shards + smoke);
`python3 do.py smoke` exit 0. Only the Windows/Vulkan acceptance line is
unticked, deferred to CI (this machine validates the macOS path only).

<model>GPT-5 Codex</model>
