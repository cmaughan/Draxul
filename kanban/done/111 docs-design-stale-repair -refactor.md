# WI 111 — docs-design-stale-repair

**Type:** refactor
**Priority:** 8 (documentation — design docs and feature table actively mislead contributors)
**Source:** review-consensus.md §4 [GPT]
**Produced by:** claude-sonnet-4-6

---

## Problem

Three documents in the current tree contain concrete, verifiably wrong information that will mislead contributors who trust them:

| Document | Wrong claim | Actual state |
|----------|-------------|--------------|
| `plans/design/renderers.md` | Describes `I3DRenderer` / `I3DHost` as the current interface | These have been refactored; the current interface is different |
| `plans/design/city_db.md` | States schema version `5` | `libs/draxul-citydb/src/citydb.cpp:30` has schema version `8` |
| `docs/features.md:131` | Command palette default binding is `Ctrl+P` | `libs/draxul-config/src/app_config_io.cpp:453` binds `Ctrl+Shift+P` |

The renderers.md issue is the most severe: it may cause contributors to implement features against a removed interface.

---

## Investigation

- [x] Read `plans/design/renderers.md` — identify which sections still reference `I3DRenderer`/`I3DHost` and determine what the current equivalent interfaces/classes are named.
- [x] Read `libs/draxul-citydb/src/citydb.cpp:25–40` — confirm the current schema version number.
- [x] Read `docs/features.md:125–140` — find the command palette keybinding entry.
- [x] Read `libs/draxul-config/src/app_config_io.cpp:445–460` — confirm the actual default binding for the palette.

---

## Fix Strategy

- [x] **`plans/design/renderers.md`:** Update the renderer interface documentation to match the current `IGridRenderer`, `IGridHandle`, `IRenderPass`, `IRenderContext` hierarchy. Mark removed concepts as "removed in [date/commit]" rather than deleting, to preserve design history.
- [x] **`plans/design/city_db.md`:** Update schema version from `5` to the current value. Add a note that schema version is auto-incremented and the document may lag.
- [x] **`docs/features.md`:** Correct the command palette default keybinding from `Ctrl+P` to `Ctrl+Shift+P`.
- [x] After edits, cross-check `docs/features.md` against `app_config_io.cpp` defaults for any other keybinding discrepancies.

---

## Acceptance Criteria

- [x] `plans/design/renderers.md` no longer references `I3DRenderer` or `I3DHost` as current interfaces.
- [x] `plans/design/city_db.md` cites the correct schema version.
- [x] `docs/features.md` shows `Ctrl+Shift+P` as the palette default.
- [x] No other keybinding entries in `features.md` contradict `app_config_io.cpp` defaults (a quick manual cross-check is sufficient).

---

## Interdependencies

None. This is a standalone documentation pass. A subagent can handle this independently given the file paths above.
