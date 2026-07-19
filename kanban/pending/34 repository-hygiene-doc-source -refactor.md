# Repository hygiene and feature-document source of truth

**Type:** refactor
**Priority:** 34
**Raised by:** Claude; supported by GPT/Codex

## Goal

Remove misleading/tracked root artifacts and establish `docs/features.md` as the single feature inventory without deleting provenance or user data blindly.

## Implementation plan

- [x] Inspect history/references for `megacity-linux-drivers-mesh.bmp`, `key.txt`, `.!75583!.DS_Store`, root logs, and `NUL.obj`; classify required asset, historical evidence, local output, or accidental artifact.
- [x] Move required large visual evidence under a documented asset/test location or external artifact store; otherwise untrack it deliberately.
- [x] Rename or remove `key.txt` after confirming it is a debug log and contains no credential material.
- [x] Add precise `.gitignore` rules for generated logs/objects without masking legitimate source assets.
- [x] Make root `FEATURES.md` a short pointer to canonical `docs/features.md` or remove it after fixing inbound links.
- [x] Keep `docs/features.md` as a short canonical inventory and move long product narratives into owned pages such as `docs/features/scoreview.md` and `docs/features/satview.md`; document `weather_location` and other real keys during the split.
- [x] Choose one canonical agent guide for shared architecture/build/tracker rules and reduce `CLAUDE.md`, `GEMINI.md`, and `learnings_agents.md` to clearly scoped additions or pointers so their instructions cannot silently diverge.
- [x] Add a plan index with `active`, `implemented`, `superseded`, and `research` status instead of leaving completed plans mixed with active designs.
- [x] Add a lightweight hygiene check for forbidden root artifacts and duplicate feature-doc content.
- [x] Generate a tracker index/status report that treats the `kanban` folder as authoritative and flags ambiguous unchecked boxes in `kanban/done` without rewriting historical cards automatically.
- [ ] Reopen the existing stale architecture-doc card separately; do not hide architecture corrections inside this cleanup. *(No such card exists in any kanban lane — see Status. Deliberately left for a human to create/reopen; no architecture correction was made here.)*

## Tests and acceptance

- [x] `git ls-files` contains no accidental OS/debug/build artifacts.
- [x] Required assets remain discoverable with attribution and build/install wiring.
- [x] Only `docs/features.md` contains the maintained feature inventory.
- [ ] Agent guides agree on the actual library/module graph and the `kanban/` tracker paths. *(Tracker paths and shared rules now agree — all guides defer to canonical `CLAUDE.md`. The library/module **graph accuracy** in `CLAUDE.md` is still stale and is deferred to the separate architecture-doc work, per the "do not hide architecture corrections here" constraint.)*
- [x] Host summary rows stay reviewable and product detail pages have stable links.
- [x] Fresh configure/build does not depend on removed files.

## Dependencies and parallelism

Independent tooling/docs lane. A sub-agent may inventory history, but any deletion of large evidence should be reviewed explicitly.

## Status — 2026-07-19

Substantially complete; **left in `pending/`** because two acceptance items are deliberately deferred to separate architecture-doc work (see the two unchecked boxes above).

Landed on branch `worktree-agent-ad2f4db9aa28624c9` in six commits:

1. Untrack root debug artifacts — `key.txt` (DPI debug log, no credential material; recoverable via `git show 518ddbf:megacity-linux-drivers-mesh.bmp`), the `.!*` transfer temp, and the 30 MB `megacity-linux-drivers-mesh.bmp` (untracked deliberately, recoverable from history).
2. Docs split — `FEATURES.md` is now a short pointer to canonical `docs/features.md`; the Score/SatView narratives moved verbatim into owned pages `docs/features/scoreview.md` and `docs/features/satview.md`; `weather_location`, `palette_bg_alpha`, `focus_border_width`, and `enable_shell_integration_marks` are documented (verified against source, defaults/ranges checked). README inbound link repointed.
3. `.gitignore` — root-anchored the `.obj` rule (`/*.obj`) so compiler objects and Windows `NUL.obj` stay ignored while mesh `*.obj` assets are not masked; documented the log/`.DS_Store` rules. `git ls-files` confirmed artifact-free.
4. Agent guides — `CLAUDE.md` is canonical (added a short "Canonical agent guide" note); `GEMINI.md` reduced from ~180 duplicated lines to a pointer; `learnings_agents.md` kept intact with a scope header marking it a retrospective. `AGENTS.md` left untouched (out of scope; shares the architecture-graph staleness).
5. `plans/INDEX.md` — new status index classifying all 41 plan docs + `prompts/`/`reviews/` as active/implemented/superseded/research; no existing plan edited.
6. `do.py` — `hygiene` (forbidden-artifact + duplicate-feature-doc check) and `kanban-report` (authoritative kanban summary; flags ambiguous `done/` cards read-only) subcommands, with 17 new tests in `tests/do_py_tests.py` (full suite: 53 passed). `docs/features.md` documents both commands.

Deferred / for the merger:

- No dedicated **stale-architecture-doc card** exists in `kanban/ice-box` (or any lane) to reopen. The staleness (CLAUDE.md's structure diagram omits libraries/`modules/`; `docs/module-map.md` points at the removed `plans/work-items/`) is only recorded as a finding in `plans/reviews/review-latest.claude.md` (§1, Bad #3). Recommend creating a fresh architecture-doc card; no architecture correction was made in this cleanup by design.

<model>GPT-5 Codex</model>
