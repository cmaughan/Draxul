# Unify the font style model

**Type:** refactor
**Priority:** 30
**Raised by:** Claude

## Goal

Replace repeated normal/bold/italic/bold-italic branches and parallel caches in `font_resolver.cpp`/`.h` with one indexed `FontStyle` model.

## Implementation plan

- [ ] Define `enum class FontStyle { Regular, Bold, Italic, BoldItalic, Count }` plus constexpr trait helpers.
- [ ] Replace parallel face/path/availability/cache members with `std::array` indexed by style.
- [ ] Consolidate load, fallback, selection, and reset loops while retaining style-specific diagnostics.
- [ ] Preserve public `TextStyle`/font APIs; keep FreeType/HarfBuzz types private.
- [ ] Audit synthetic-style behavior and fallback precedence for each style.
- [ ] Remove obsolete duplicated methods only after parity tests cover all four styles.

## Tests and acceptance

- [ ] Table-test resolution/fallback/cache invalidation for every style and missing variant combination.
- [ ] Verify config round-trip paths remain mapped correctly.
- [ ] Render ligature/style references without blessing unrelated differences.
- [ ] Build/font tests/ctest/smoke pass on both platform paths.

## Dependencies and parallelism

Independent font refactor; avoid overlap with active text-atlas changes until they settle.

<model>GPT-5 Codex</model>
